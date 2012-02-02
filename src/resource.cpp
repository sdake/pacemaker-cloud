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

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>

#include <iostream>
#include <sstream>
#include <map>

#include "pcmk_pe.h"
#include "mainloop.h"
#include "resource.h"
#include "assembly_am.h"
#include "deployable.h"

using namespace std;
using namespace qmf;

static void
resource_interval_timeout(gpointer data)
{
	struct pe_operation *op = (struct pe_operation *)data;
	Resource *rsc = (Resource *)op->resource;

	if (op->refcount == 1) {
		// we are the last ref holder
		pe_resource_unref(op);
		return;
	}

	rsc->__execute(op);

	mainloop_timer_add(op->interval, data,
			   resource_interval_timeout,
			   &rsc->_monitor_timer);
}

void
Resource::failed(struct pe_operation *op)
{
	string ass_name = op->hostname;
	string rname = op->rtype;
	string running = "failed";
	string reason = "monitor failed";
	bool escalated = false;

	_state = STATE_STOPPED;
	_dep->service_state_changed(ass_name, rname, running, reason);
	if (_max_failures > 0 && _failure_period > 0) {
		uint64_t diff;
		float p;
		int last;
		qb_util_stopwatch_split(_escalation_period);

		last = qb_util_stopwatch_split_last(_escalation_period);
		qb_log(LOG_NOTICE, "resource %s FAILED (%d)",
				_id.c_str(), last + 1);
		if (last < _max_failures - 1) {
			return;
		}
		diff = qb_util_stopwatch_time_split_get(_escalation_period, last,
							last - (_max_failures - 1));
		p = diff / QB_TIME_US_IN_SEC;
		if (p <= _failure_period) {
			string node_uuid = op->node_uuid;
			AssemblyAm *ass = dynamic_cast<AssemblyAm*>(_dep->assembly_get(node_uuid));
			_dep->escalate_service_failure(ass, rname);
			qb_util_stopwatch_start(_escalation_period);
			escalated = true;
			_state = STATE_UNKNOWN;
		}
	}
	if (!escalated) {
		_dep->schedule_processing();
	}
}

void
Resource::execute(struct pe_operation *op)
{
	op->resource = this;
	__execute(op);
}

void
Resource::stop(struct pe_operation *op)
{
	if (mainloop_timer_is_running(_monitor_timer)) {
		pe_resource_unref(_monitor_op);
		mainloop_timer_del(_monitor_timer);
	}
	op->resource = this;
	__execute(op);
}

void
Resource::start_recurring(struct pe_operation *op)
{
	qb_enter();

	op->resource = this;
	if (!mainloop_timer_is_running(_monitor_timer)) {
		__execute(op);

		op->user_data = this;
		_monitor_op = op;
		pe_resource_ref(op);
		mainloop_timer_add(op->interval,
				   op, resource_interval_timeout,
				   &_monitor_timer);
	} else {
		pe_resource_completed(op, OCF_OK);
		pe_resource_unref(op); // delete
	}
}

static void
g_hash_to_variant_map(gpointer key, gpointer value, gpointer user_data)
{
	qpid::types::Variant::Map* the_args = (qpid::types::Variant::Map*)user_data;
	char * s_key = (char *)key;
	char * s_val = (char *)value;
	(*the_args)[s_key] = s_val;
}

void
Resource::__execute(struct pe_operation *op)
{
	Agent a;
	bool is_monitor_op = false;
	qpid::types::Variant::Map in_args;
	qpid::types::Variant::Map in_params;
	const char *rmethod = op->method;
	string node_uuid = op->node_uuid;
	AssemblyAm *ass = dynamic_cast<AssemblyAm*>(_dep->assembly_get(node_uuid));
	assert(ass != NULL);

	qb_enter();

	if (ass->state_get() != Assembly::STATE_ONLINE) {
		qb_log(LOG_DEBUG, "can't execute resource in offline state");
		completed(op, OCF_UNKNOWN_ERROR);
		return;
	}

	if (strcmp(op->method, "monitor") == 0) {
		is_monitor_op = true;
	}
	if (strstr(_id.c_str(), op->hostname) == NULL) {
		if (is_monitor_op) {
			completed(op, OCF_NOT_RUNNING);
		} else {
			completed(op, OCF_UNKNOWN_ERROR);
		}
		qb_leave();
		return;
	}
	in_args["timeout"] = op->timeout;

	if (strcmp(op->rclass, "lsb") == 0) {
		if (is_monitor_op) {
			rmethod = "status";
			in_args["interval"] = 0;
		}
		in_args["name"] = op->rtype;
		pe_resource_ref(op);
		ass->service_execute(op, rmethod, in_args);
	} else {
		if (is_monitor_op) {
			rmethod = "monitor";
		} else {
			rmethod = "invoke";
			in_args["action"] = op->method;
		}
		in_args["interval"] = 0;
		// make a non-empty parameters map
		in_params["qmf"] = "frustrating";
		g_hash_table_foreach(op->params, g_hash_to_variant_map, &in_params);

		in_args["name"] = op->rname;
		in_args["class"] = op->rclass;
		if (op->rprovider == NULL) {
			in_args["provider"] = "heartbeat";
		} else {
			in_args["provider"] = op->rprovider;
		}
		in_args["type"] = op->rtype;
		in_args["parameters"] = in_params;
		pe_resource_ref(op);
		ass->resource_execute(op, rmethod, in_args);
	}
	qb_leave();
}

xmlNode *
Resource::insert_status(xmlNode *rscs)
{
	xmlNode *rsc = xmlNewChild(rscs, NULL, BAD_CAST "lrm_resource", NULL);
	xmlNewProp(rsc, BAD_CAST "id", BAD_CAST _id.c_str());
	xmlNewProp(rsc, BAD_CAST "type", BAD_CAST _type.c_str());
	xmlNewProp(rsc, BAD_CAST "class", BAD_CAST _class.c_str());
	if (strcmp(_class.c_str(), "ocf") == 0) {
		xmlNewProp(rsc, BAD_CAST "provider", BAD_CAST _provider.c_str());
	}
	return rsc;
}

void
Resource::delete_op_history(struct pe_operation *op)
{
	string node_uuid = op->node_uuid;
	AssemblyAm *a = dynamic_cast<AssemblyAm*>(_dep->assembly_get(node_uuid));

	/* delete the op history
	 */
	a->op_history_del_by_resource(this);

	/* stop the recurring monitor.
	 */
	if (mainloop_timer_is_running(_monitor_timer)) {
		pe_resource_unref(_monitor_op);
		mainloop_timer_del(_monitor_timer);
	}
	qb_log(LOG_DEBUG, "%s_%s_%d [%s] on %s rc:0 target_rc:%d",
	       op->rname, op->method, op->interval, op->rclass, op->hostname,
	       op->target_outcome);
	pe_resource_completed(op, OCF_OK);
	pe_resource_unref(op);
}

void
Resource::completed(struct pe_operation *op, enum ocf_exitcode ec)
{
	string node_uuid = op->node_uuid;
	AssemblyAm *ass = dynamic_cast<AssemblyAm*>(_dep->assembly_get(node_uuid));
	assert(ass != NULL);

	if (ec != op->target_outcome) {
		qb_log(LOG_DEBUG, "%s_%s_%d [%s] on %s rc:%d (expecting:%d)",
		       op->rname, op->method, op->interval, op->rclass, op->hostname,
		       ec, op->target_outcome);
	}

	if (ass->state_get() != Assembly::STATE_ONLINE) {
		_state = STATE_UNKNOWN;
		if (op->interval == 0) {
			pe_resource_completed(op, OCF_UNKNOWN_ERROR);
		}
		pe_resource_unref(op);
		_dep->schedule_processing();
		return;
	}

	if (strstr(op->rname, op->hostname) != NULL) {
		ass->op_history_save(this, op, ec);
	}

	if (ec != op->target_outcome) {
		_dep->schedule_processing();
	}
	if (op->interval == 0) {
		if (strcmp(op->method, "start") == 0) {
			string rname = op->rtype;
			string running;
			string reason;
			if (ec == OCF_OK) {
				running = "running";
				reason = "Started OK";
				_state = STATE_RUNNING;
			} else {
				running = "failed";
				reason = pe_resource_reason_get(ec);
				_state = STATE_STOPPED;
			}
			_dep->service_state_changed(ass->name_get(), rname, running, reason);
		} else if (pe_resource_is_hard_error(ec)) {
			string rname = op->rtype;
			string running = "failed";
			string reason = pe_resource_reason_get(ec);
			_dep->service_state_changed(ass->name_get(), rname, running, reason);
			_state = STATE_STOPPED;
		}

		pe_resource_completed(op, ec);
		pe_resource_unref(op);
		return;
	}

	if (op->times_executed <= 1) {
		pe_resource_completed(op, ec);
	} else if (ec != op->target_outcome) {
		failed(op);
	}
	if (strcmp(op->method, "monitor") == 0 ||
	    strcmp(op->method, "status") == 0) {
		string rname = op->rtype;
		string running;
		string reason;
		if (ec == op->target_outcome &&
		    _state != STATE_RUNNING) {
			running = "running";
			reason = "Already running";
			_state = STATE_RUNNING;
			_dep->service_state_changed(ass->name_get(), rname, running, reason);
		} else if (op->times_executed <= 1 &&
			   ec != op->target_outcome &&
			   _state != STATE_STOPPED) {
			running = "stopped";
			reason = "Not yet running";
			_state = STATE_STOPPED;
			_dep->service_state_changed(ass->name_get(), rname, running, reason);
		}
	}
}

Resource::Resource(Deployable *d, string& id, string& type, string& class_name,
		   string& provider, int num_failures, int failure_period)
:  _dep(d), _id(id), _type(type), _class(class_name), _provider(provider),
   _max_failures(num_failures), _failure_period(failure_period),
   _actual_failures(0), _state(STATE_UNKNOWN)
{
	if (_max_failures > 0 && _failure_period > 0) {
		_escalation_period = qb_util_stopwatch_create();
		qb_util_stopwatch_start(_escalation_period);
	}
}

Resource::~Resource()
{
	if (_max_failures > 0 && _failure_period > 0) {
		qb_util_stopwatch_free(_escalation_period);
	}
}
