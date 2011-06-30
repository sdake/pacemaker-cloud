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

#include <qb/qblog.h>

#include <iostream>
#include <sstream>
#include <map>

#include "pcmk_pe.h"
#include "mainloop.h"
#include "resource.h"
#include "assembly.h"
#include "deployable.h"

static uint32_t call_order = 0;

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
	qb_log(LOG_NOTICE, "resource %s FAILED", _id.c_str());

	_dep->service_state_changed(ass_name, rname, running, reason);
	_dep->schedule_processing();
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

void
Resource::__execute(struct pe_operation *op)
{
	Agent a;
	qpid::types::Variant::Map in_args;
	qpid::types::Variant::Map in_params;
	const char *rmethod = op->method;
	string node_uuid = op->node_uuid;
	Assembly *ass = _dep->assembly_get(node_uuid);
	assert(ass != NULL);

	qb_enter();

	if (ass->state_get() != Assembly::STATE_ONLINE) {
		qb_log(LOG_DEBUG, "can't execute resource in offline state");
		completed(op, OCF_UNKNOWN_ERROR);
		return;
	}
	if (strstr(_id.c_str(), op->hostname) == NULL) {
		if (strcmp(op->method, "monitor") == 0) {
			completed(op, OCF_NOT_RUNNING);
		} else {
			completed(op, OCF_UNKNOWN_ERROR);
		}
		qb_leave();
		return;
	}
	if (strcmp(op->method, "monitor") == 0) {
		in_args["interval"] = 0;
	}
	if (op->timeout < 20000) {
		in_args["timeout"] = 20000;
	} else {
		in_args["timeout"] = op->timeout;
	}

	if (strcmp(op->rclass, "lsb") == 0) {
		if (strcmp(op->method, "monitor") == 0) {
			rmethod = "status";
		}
		in_args["name"] = op->rtype;
		pe_resource_ref(op);
		ass->resource_execute(op, rmethod, in_args);
	} else {
		// make a non-empty parameters map
		in_params["qmf"] = "frustrating";

		in_args["name"] = op->rname;
		in_args["class"] = op->rclass;
		in_args["provider"] = op->rprovider;
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
Resource::save(struct pe_operation *op, enum ocf_exitcode ec)
{
	struct operation_history *oh;
	stringstream id;
	string node_uuid = op->node_uuid;
	Assembly* a = _dep->assembly_get(node_uuid);

	if (strstr(op->rname, op->hostname) == NULL) {
		return;
	}

	id << op->rname << "_" << op->method << "_" << op->interval;

	oh = a->op_history[id.str()];
	if (oh == NULL) {
		oh = (struct operation_history *)calloc(1, sizeof(struct operation_history));
		oh->resource = this;
		oh->rsc_id = new string(id.str());
		oh->operation = new string(op->method);
		oh->target_outcome = op->target_outcome;
		oh->interval = op->interval;
		oh->rc = OCF_PENDING;
		oh->op_digest = op->op_digest;

		a->op_history[id.str()] = oh;
	} else if (strcmp(oh->op_digest, op->op_digest) != 0) {
		free(oh->op_digest);
		oh->op_digest = op->op_digest;
	}
	if (oh->rc != ec) {
		oh->last_rc_change = time(NULL);
		oh->rc = ec;
	}

	oh->last_run = time(NULL);
	oh->call_id = call_order++;
	oh->graph_id = op->graph_id;
	oh->action_id = op->action_id;
}

void
Resource::delete_op_history(struct pe_operation *op)
{
	string node_uuid = op->node_uuid;
	Assembly* a = _dep->assembly_get(node_uuid);

	/* delete the op history
	 */
	for (map<string, struct operation_history*>::iterator iter = a->op_history.begin();
	     iter != a->op_history.end(); iter++) {
		struct operation_history *oh = iter->second;
		if (this == oh->resource) {
			a->op_history.erase(iter);
		}
	}
	/* stop the recurring monitor.
	 */
	if (mainloop_timer_is_running(_monitor_timer)) {
		pe_resource_unref(_monitor_op);
		mainloop_timer_del(_monitor_timer);
	}
	qb_log(LOG_INFO, "%s_%s_%d [%s] on %s rc:0 target_rc:%d",
	       op->rname, op->method, op->interval, op->rclass, op->hostname,
	       op->target_outcome);
	pe_resource_completed(op, OCF_OK);
	pe_resource_unref(op);
}

void
Resource::completed(struct pe_operation *op, enum ocf_exitcode ec)
{
	string node_uuid = op->node_uuid;
	Assembly *ass = _dep->assembly_get(node_uuid);
	assert(ass != NULL);

	qb_log(LOG_INFO, "%s_%s_%d [%s] on %s rc:%d target_rc:%d",
	       op->rname, op->method, op->interval, op->rclass, op->hostname,
	       ec, op->target_outcome);

	if (ass->state_get() != Assembly::STATE_ONLINE) {
		if (op->interval == 0) {
			pe_resource_completed(op, OCF_UNKNOWN_ERROR);
		}
		pe_resource_unref(op);
		_dep->schedule_processing();
		return;
	}

	save(op, ec);

	if (ec != op->target_outcome) {
		_dep->schedule_processing();
	}
	if (op->interval == 0) {
		if (strcmp(op->method, "start") == 0 && ec == OCF_OK) {
			string rname = op->rtype;
			string running = "running";
			string reason = "started OK";
			_dep->service_state_changed(ass->name_get(), rname, running, reason);
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
}

Resource::Resource(Deployable *d, string& id, string& type, string& class_name,
		   string& provider)
:  _dep(d), _id(id), _type(type), _class(class_name), _provider(provider)
{
}

