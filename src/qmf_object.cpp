/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of pacemaker-cloud.
 *
 * pacemaker-cloud is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * pacemaker-cloud is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pacemaker-cloud.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <qb/qblog.h>
#include <qb/qbloop.h>
#include "mainloop.h"
#include "qmf_job.h"
#include "qmf_object.h"

using namespace std;
using namespace qmf;

ConsoleEvent
QmfObject::method_call(std::string method,
		       qpid::types::Variant::Map in_args)
{
	Agent a = _qmf_data.getAgent();
	return a.callMethod(method, in_args, _qmf_data.getAddr());
}

static void
run_pending_calls_fn(void* data)
{
	QmfObject *o = (QmfObject *)data;

	o->run_pending_calls();
}

static void
method_call_tmo(void* data)
{
	QmfAsyncRequest *ar = (QmfAsyncRequest *)data;
	gdouble elapsed = 0;

	if (ar->state == QmfAsyncRequest::JOB_COMPLETED) {
		// all good - it's finished
	} else {
		// timed out :(
		qpid::types::Variant::Map empty_args;
		if (ar->state == QmfAsyncRequest::JOB_SCHEDULED) {
			ar->obj->method_response(ar, empty_args, QmfObject::RPC_NOT_CONNECTED);
		} else {
			ar->obj->method_response(ar, empty_args, QmfObject::RPC_TIMEOUT);
		}
	}
	ar->unref();
}

void
QmfObject::run_pending_calls(void)
{
	Agent a = _qmf_data.getAgent();
	for (list<QmfAsyncRequest*>::iterator it = _pending_jobs.begin();
		     it != _pending_jobs.end(); ++it) {
		QmfAsyncRequest* ar = *it;
		qb_loop_timer_handle th;
		uint32_t correlation_id = a.callMethodAsync(ar->method, ar->args, _qmf_data.getAddr());
		_outstanding_calls[correlation_id] = ar;
		g_timer_stop(ar->time_queued);
		g_timer_start(ar->time_execed);
		ar->state = QmfAsyncRequest::JOB_RUNNING;
		ar->ref();
		mainloop_timer_add(ar->timeout, ar,
				   method_call_tmo, &th);
	}
}

void
QmfObject::method_call_async(std::string method,
			     qpid::types::Variant::Map in_args,
			     void *user_data,
			     uint32_t timeout_ms)
{
	uint32_t correlation_id;
	QmfAsyncRequest *ar;

	if (_method_response_fn == NULL) {
		qb_log(LOG_WARNING,
		       "can't do async call without response callback");
		return;
	}

	ar = new QmfAsyncRequest();
	ar->method = method;
	ar->args = in_args;
	ar->obj = this;
	ar->user_data = user_data;
	ar->timeout = timeout_ms;

	if (_connected) {
		qb_loop_timer_handle th;
		Agent a = _qmf_data.getAgent();
		correlation_id = a.callMethodAsync(method, in_args, _qmf_data.getAddr());
		_outstanding_calls[correlation_id] = ar;
		g_timer_stop(ar->time_queued);
		g_timer_start(ar->time_execed);
		ar->state = QmfAsyncRequest::JOB_RUNNING;
		ar->ref();
		mainloop_timer_add(timeout_ms, ar,
				   method_call_tmo, &th);
	} else {
		ar->state = QmfAsyncRequest::JOB_SCHEDULED;
		g_timer_start(ar->time_queued);
		_pending_jobs.push_back(ar);
	}
}

bool
QmfObject::process_event(ConsoleEvent &event)
{
	QmfAsyncRequest *ar;

	if (event.getType() == CONSOLE_METHOD_RESPONSE) {
		ar = _outstanding_calls[event.getCorrelator()];
		if (ar) {
			qpid::types::Variant::Map my_map = event.getArguments();
			if (ar->state == QmfAsyncRequest::JOB_RUNNING) {
				method_response(ar, my_map, RPC_OK);
			} else {
				qb_log(LOG_ERR, " method_response is too late! ");
			}
			_outstanding_calls.erase(event.getCorrelator());
			ar->unref();
		}
	} else if (event.getType() == CONSOLE_EXCEPTION) {
		ar = _outstanding_calls[event.getCorrelator()];
		if (ar) {
			qpid::types::Variant::Map my_map;
			string error(" unknown ");

			if (event.getDataCount() >= 1) {
				my_map = event.getData(0).getProperties();
				error = (string)event.getData(0).getProperty("error_text");
			}
			qb_log(LOG_ERR, "%s'ing: EXCEPTION %s ",
			       ar->method.c_str(), error.c_str());

			method_response(ar, my_map, RPC_EXCEPTION);
			_outstanding_calls.erase(event.getCorrelator());
			ar->unref();
		}
	} else if (event.getType() == CONSOLE_EVENT) {
		if (_event_fn != NULL) {
			_event_fn(event, _event_user_data);
		}
	}

	return true;
}

void
QmfAsyncRequest::log(void)
{
	gdouble queued = 0;
	gdouble execed = 0;
	queued = g_timer_elapsed(time_queued, NULL);
	execed = g_timer_elapsed(time_execed, NULL);
	qb_log(LOG_DEBUG, "%s state:%d time_queued:%f run_time:%f (timeout:%d)",
	       method.c_str(), state, queued, execed, timeout);
}

bool
QmfObject::connect(Agent &a)
{
	if (_connected) {
		return false;
	}
	ConsoleEvent ce = a.query(_query);
	for (int q = 0; q < ce.getDataCount(); q++) {
		if (_prop_name.length() > 0) {
			string prop_val = ce.getData(q).getProperty(_prop_name);
			if (prop_val == _prop_value) {
				_connected = true;
			}
		} else {
			_connected = true;
		}
		if (_connected) {
			_agent_name = ce.getAgent().getName();
			_qmf_data = ce.getData(q);
			mainloop_job_add(QB_LOOP_LOW, this,
					 run_pending_calls_fn);

			if (_connection_event_fn) {
				_connection_event_fn(_connection_event_user_data);
			}
			return true;
		}
	}
	return false;
}

void
QmfObject::disconnect(void)
{
	QmfAsyncRequest *ar;
	qpid::types::Variant::Map empty_args;
	map<uint32_t, QmfAsyncRequest*>::iterator iter;

	if (!_connected) {
		return;
	}
	_connected = false;
	_agent_name = "";

	for (iter = _outstanding_calls.begin();
	     iter != _outstanding_calls.end(); iter++) {
		ar = iter->second;
		method_response(ar, empty_args, QmfObject::RPC_CANCELLED);
	}

//	if (_connection_event_fn) {
//		_connection_event_fn(_connection_event_user_data);
//	}
}
