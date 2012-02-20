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
#include "config.h"

#include <assert.h>

#include <qb/qblog.h>
#include <qb/qbloop.h>
#include "qmf_job.h"
#include "qmf_object.h"
#include "qmf_agent.h"

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
	qb_loop_timer_handle th;
	QmfAsyncRequest* ar;
	Agent a = _qmf_data.getAgent();

	for (list<QmfAsyncRequest*>::iterator it = _pending_jobs.begin();
		     it != _pending_jobs.end(); ++it) {
		ar = *it;
		_qa->call_method_async(ar, &_qmf_data);
		g_timer_stop(ar->time_queued);
		g_timer_start(ar->time_execed);
		ar->state = QmfAsyncRequest::JOB_RUNNING;
		ar->ref();
		qb_loop_timer_add(NULL, QB_LOOP_MED, ar->timeout * QB_TIME_NS_IN_MSEC,
				  ar, method_call_tmo, &th);
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

	assert(_method_response_fn);
	ar = new QmfAsyncRequest();
	ar->method = method;
	ar->obj = this;
	ar->user_data = user_data;
	ar->timeout = timeout_ms;
	ar->args = in_args;

	if (_connected) {
		qb_loop_timer_handle th;
		_qa->call_method_async(ar, &_qmf_data);
		g_timer_stop(ar->time_queued);
		g_timer_start(ar->time_execed);
		ar->state = QmfAsyncRequest::JOB_RUNNING;
		ar->ref();
		qb_loop_timer_add(NULL, QB_LOOP_MED,
				  timeout_ms * QB_TIME_NS_IN_MSEC, ar,
				  method_call_tmo, &th);
	} else {
		ar->state = QmfAsyncRequest::JOB_SCHEDULED;
		g_timer_start(ar->time_queued);
		_pending_jobs.push_back(ar);
	}
}

void
QmfObject::process_event(ConsoleEvent &event, QmfAsyncRequest *ar)
{
	if (event.getType() == CONSOLE_METHOD_RESPONSE) {
		qpid::types::Variant::Map my_map = event.getArguments();
		if (ar->state == QmfAsyncRequest::JOB_RUNNING) {
			method_response(ar, my_map, RPC_OK);
		} else {
			qb_log(LOG_NOTICE, " method_response is too late! ");
		}
		ar->unref();
	} else if (event.getType() == CONSOLE_EXCEPTION) {
		qpid::types::Variant::Map my_map;
		string error(" unknown ");

		if (event.getDataCount() >= 1) {
			my_map = event.getData(0).getProperties();
			error = (string)event.getData(0).getProperty("error_text");
		}
		qb_log(LOG_INFO, "%s'ing: EXCEPTION %s ",
		       ar->method.c_str(), error.c_str());

		method_response(ar, my_map, RPC_EXCEPTION);
		ar->unref();
	} else if (event.getType() == CONSOLE_EVENT) {
		if (_event_fn != NULL) {
			_event_fn(event, _event_user_data);
		}
	}
}

void
QmfAsyncRequest::log(int rc)
{
	gdouble queued = 0;
	gdouble execed = 0;
	queued = g_timer_elapsed(time_queued, NULL);
	execed = g_timer_elapsed(time_execed, NULL);
	qb_log(LOG_INFO, "%s state:%d time_queued:%f run_time:%f (timeout:%d) rc:%d",
	       method.c_str(), state, queued, execed, timeout, rc);
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
			if (strcasecmp(prop_val.c_str(), _prop_value.c_str()) == 0) {
				_connected = true;
			} else {
				qb_log(LOG_TRACE, "[prop: %s] %s != %s",
				       _prop_name.c_str(), _prop_value.c_str(), prop_val.c_str());
			}
		} else {
			_connected = true;
		}
		if (_connected) {
			_agent_name = ce.getAgent().getName();
			_qmf_data = ce.getData(q);
			qb_loop_job_add(NULL, QB_LOOP_LOW, this,
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
	_connected = false;
	_agent_name = "";
	_qa = NULL;
}
