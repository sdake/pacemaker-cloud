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
#include "assembly.h"
#include "resource.h"
#include "deployable.h"

static uint32_t call_order = 0;

using namespace std;
using namespace qmf;

static void
service_method_response(QmfAsyncRequest* ar,
			qpid::types::Variant::Map out_args,
			enum QmfObject::rpc_result rpc_rc)
{
	enum ocf_exitcode rc;
	struct pe_operation *op;
	Resource *rsc;

	if (rpc_rc == QmfObject::RPC_OK) {
		op = (struct pe_operation *)ar->user_data;
		rsc = (Resource *)op->resource;
		rc = pe_resource_ocf_exitcode_get(op, out_args["rc"].asUint32());
		op->times_executed++;

		rsc->completed(op, rc);
		// remove the ref for putting the op in the map
		pe_resource_unref(op);
	} else {
		rc = OCF_UNKNOWN_ERROR;
		op = (struct pe_operation *)ar->user_data;
		rsc = (Resource *)op->resource;

		if (out_args.size() >= 1) {
			string error(out_args["error_text"]);
			qb_log(LOG_ERR, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%s",
			       op->method, op->rname, op->rclass, op->rtype, op->hostname,
			       op->interval, error.c_str());
		} else {
			qb_log(LOG_ERR, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%d",
			       op->method, op->rname, op->rclass, op->rtype, op->hostname,
			       op->interval, rc);
		}
		rsc->completed(op, rc);
	}
}

static void
connection_event_handler(void *user_data)
{
	Assembly *a = (Assembly*)user_data;
	a->check_state();
}

static void
heartbeat_check_tmo(void *data)
{
	Assembly *a = (Assembly*)data;
	qb_loop_timer_handle th;

	a->check_state();
	if (a->state_get() == Assembly::STATE_ONLINE) {
		mainloop_timer_add(4000, a,
				   heartbeat_check_tmo, &th);
	}
}

static void
host_event_handler(ConsoleEvent &event, void *user_data)
{
	Assembly *a = (Assembly*)user_data;
	const Data& event_data(event.getData(0));

	if (event_data.getSchemaId().getName() == "heartbeat") {
		uint32_t seq = event_data.getProperty("sequence");
		uint32_t tstamp = event_data.getProperty("timestamp");

		a->heartbeat_recv(tstamp, seq);
		a->check_state();
	}
}

void
Assembly::resource_execute(struct pe_operation *op, std::string method,
			   qpid::types::Variant::Map in_args)
{
	_mh_serv.method_call_async(method, in_args, op, op->timeout);
}

void
Assembly::deref(void)
{
	_refcount--;
	if (_refcount == 0) {
		delete this;
	}
}

static void
xml_new_int_prop(xmlNode *n, const char *name, int32_t val)
{
	char int_str[36];
	snprintf(int_str, 36, "%d", val);
	xmlNewProp(n, BAD_CAST name, BAD_CAST int_str);
}

static void
xml_new_time_prop(xmlNode *n, const char *name, time_t val)
{
	char int_str[36];
	snprintf(int_str, 36, "%d", val);
	xmlNewProp(n, BAD_CAST name, BAD_CAST int_str);
}

/*
 * id			Identifier for the job constructed from the resource id, operation and interval.
 * call-id	 	The job's ticket number. Used as a sort key to determine the order in which the jobs were executed.
 * operation		The action the resource agent was invoked with.
 * interval		The frequency, in milliseconds, at which the operation will be repeated. 0 indicates a one-off job.
 * op-status		The job's status. Generally this will be either 0 (done) or -1 (pending). Rarely used in favor of rc-code.
 * rc-code	 	The job's result.
 * last-run		Diagnostic indicator. Machine local date/time, in seconds since epoch,
 * 			at which the job was executed.
 * last-rc-change	Diagnostic indicator. Machine local date/time, in seconds since epoch,
 * 			at which the job first returned the current value of rc-code
 * exec-time		Diagnostic indicator. Time, in seconds, that the job was running for
 * queue-time		Diagnostic indicator. Time, in seconds, that the job was queued for in the LRMd
 * crm_feature_set	The version which this job description conforms to. Used when processing op-digest
 * transition-key	A concatenation of the job's graph action number, the graph number,
 * 			the expected result and the UUID of the crmd instance that scheduled it.
 * 			This is used to construct transition-magic (below).
 * transition-magic	A concatenation of the job's op-status, rc-code and transition-key.
 * 			Guaranteed to be unique for the life of the cluster (which ensures it is part of CIB update notifications)
 * 			and contains all the information needed for the crmd to correctly analyze and process the completed job.
 * 			Most importantly, the decomposed elements tell the crmd if the job entry was expected and whether it failed.
 * op-digest		An MD5 sum representing the parameters passed to the job.
 * 			Used to detect changes to the configuration and restart resources if necessary.
 * crm-debug-origin	Diagnostic indicator. The origin of the current values.
 */
/* <lrm_resource id="pingd:0" type="pingd" class="ocf" provider="pacemaker">
 * <lrm_rsc_op
 *  id="pingd:0_monitor_30000"
 *  operation="monitor"
 *  call-id="34"
 *  rc-code="0"
 *  interval="30000"
 *  crm-debug-origin="do_update_resource"
 *  crm_feature_set="3.0.1"
 *  op-digest="a0f8398dac7ced82320fe99fd20fbd2f"
 *  transition-key="10:11:0:2668bbeb-06d5-40f9-936d-24cb7f87006a"
 *  transition-magic="0:0;10:11:0:2668bbeb-06d5-40f9-936d-24cb7f87006a"
 *  last-run="1239009741"
 *  last-rc-change="1239009741"
 *  exec-time="10"
 *  queue-time="0"/>
 */
void
Assembly::op_history_insert(xmlNode *rsc, struct operation_history *oh)
{
	xmlNode *op;
	char key[255];
	char magic[255];

	op = xmlNewChild(rsc, NULL, BAD_CAST "lrm_rsc_op", NULL);

	xmlNewProp(op, BAD_CAST "id", BAD_CAST oh->rsc_id->c_str());
	xmlNewProp(op, BAD_CAST "operation", BAD_CAST oh->operation->c_str());
	xml_new_int_prop(op, "call-id", oh->call_id);
	xml_new_int_prop(op, "rc-code", oh->rc);
	xml_new_int_prop(op, "interval", oh->interval);
	xml_new_time_prop(op, "last-run", oh->last_run);
	xml_new_time_prop(op, "last-rc-change", oh->last_rc_change);

	snprintf(key, 255, "%d:%d:%d:%s",
		 oh->action_id, oh->graph_id, oh->target_outcome,
		 _dep->crmd_uuid_get().c_str());
	xmlNewProp(op, BAD_CAST "transition-key", BAD_CAST key);

	snprintf(magic, 255, "0:%d:%s", oh->rc, key);
	xmlNewProp(op, BAD_CAST "transition-magic", BAD_CAST magic);

	xmlNewProp(op, BAD_CAST "op-digest", BAD_CAST oh->op_digest);
	xmlNewProp(op, BAD_CAST "crm-debug-origin", BAD_CAST __func__);
	xmlNewProp(op, BAD_CAST "crm_feature_set", BAD_CAST PE_CRM_VERSION);
	xmlNewProp(op, BAD_CAST "op-status", BAD_CAST "0");
	xmlNewProp(op, BAD_CAST "exec-time", BAD_CAST "0");
	xmlNewProp(op, BAD_CAST "queue-time", BAD_CAST "0");
}

void
Assembly::op_history_save(Resource* r, struct pe_operation *op,
			  enum ocf_exitcode ec)
{
	struct operation_history *oh;
	stringstream id;

	id << op->rname << "_" << op->method << "_" << op->interval;

	oh = op_history[id.str()];
	if (oh == NULL) {
		oh = (struct operation_history *)calloc(1, sizeof(struct operation_history));
		oh->resource = r;
		oh->rsc_id = new string(id.str());
		oh->operation = new string(op->method);
		oh->target_outcome = op->target_outcome;
		oh->interval = op->interval;
		oh->rc = OCF_PENDING;
		oh->op_digest = op->op_digest;

		op_history[id.str()] = oh;
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
Assembly::op_history_del_by_resource(Resource* r)
{
	for (map<string, struct operation_history*>::iterator iter = op_history.begin();
	     iter != op_history.end(); iter++) {
		struct operation_history *oh = iter->second;
		if (r == oh->resource) {
			op_history.erase(iter);
			delete oh->rsc_id;
			delete oh->operation;
			free(oh);
		}
	}
}

void
Assembly::op_history_clear(void)
{
	for (map<string, struct operation_history*>::iterator iter = op_history.begin();
	     iter != op_history.end(); iter++) {
		struct operation_history *oh = iter->second;
		op_history.erase(iter);
		delete oh->rsc_id;
		delete oh->operation;
		free(oh);
	}
}

void
Assembly::insert_status(xmlNode *status)
{
	xmlNode *node_state = xmlNewChild(status, NULL, BAD_CAST "node_state", NULL);
	Resource *r = NULL;

	qb_enter();
	xmlNewProp(node_state, BAD_CAST "id", BAD_CAST _uuid.c_str());
	xmlNewProp(node_state, BAD_CAST "uname", BAD_CAST _name.c_str());

	xmlNewProp(node_state, BAD_CAST "ha", BAD_CAST "active");
	xmlNewProp(node_state, BAD_CAST "expected", BAD_CAST "member");
	xmlNewProp(node_state, BAD_CAST "in_ccm", BAD_CAST "true");
	xmlNewProp(node_state, BAD_CAST "crmd", BAD_CAST "online");

	if (_state == STATE_ONLINE) {
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "member");
	} else {
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "pending");
	}
	if (op_history.size() > 0) {
		xmlNode *lrm = xmlNewChild(node_state, NULL, BAD_CAST "lrm", NULL);
		xmlNode *rscs = xmlNewChild(lrm, NULL, BAD_CAST "lrm_resources", NULL);

		for (map<string, struct operation_history*>::iterator iter = op_history.begin();
		     iter != op_history.end(); iter++) {
			struct operation_history *oh = iter->second;
			xmlNode *rsc = NULL;
			if (r != oh->resource) {
				r = (Resource*)oh->resource;
				rsc = r->insert_status(rscs);
			}
			op_history_insert(rsc, oh);
		}
	}

	qb_leave();
}

void
Assembly::check_state(void)
{
	uint32_t new_state = (this->*state_table[_state])();

	if (state_action_table[_state][new_state]) {
		(this->*state_action_table[_state][new_state])();
	}
	if (_state != new_state) {
		_state = new_state;
	}
}

uint32_t
Assembly::check_state_online(void)
{
	uint32_t new_state = _state;
	gdouble elapsed = 0;

	if (_hb_state == HEARTBEAT_OK) {
		elapsed = g_timer_elapsed(_last_heartbeat, NULL);
		if (elapsed > (5 * 1.5)) {
			_hb_state = Assembly::HEARTBEAT_NOT_RECEIVED;
			qb_log(LOG_WARNING,
			       "assembly (%s) heartbeat too late! (%.2f > 5 seconds)",
			       _name.c_str(), elapsed);
		}
	}
	if (_hb_state != HEARTBEAT_OK) {
		new_state = STATE_OFFLINE;
	}
	return new_state;
}

uint32_t
Assembly::check_state_offline(void)
{
	uint32_t new_state = _state;
	if (_hb_state == HEARTBEAT_OK &&
	    _mh_serv.is_connected() &&
	    _mh_host.is_connected()) {
		new_state = STATE_ONLINE;
	}
	return new_state;
}

void
Assembly::state_offline_to_online(void)
{
	qb_loop_timer_handle th;

	qb_log(LOG_NOTICE, "Assembly (%s) STATE_ONLINE.",
	       _name.c_str());
	_dep->assembly_state_changed(this, "running", "All good");

	mainloop_timer_add(4000, this,
			   heartbeat_check_tmo, &th);
}

void
Assembly::state_online_to_offline(void)
{
	_mh_serv.disconnect();
	_mh_host.disconnect();

	/* re-init the heartbeat state
	 */
	_hb_state = Assembly::HEARTBEAT_INIT;

	/* kill the resource history
	 */
	op_history_clear();

	qb_log(LOG_NOTICE, "Assembly (%s) STATE_OFFLINE.",
	       _name.c_str());
	_dep->assembly_state_changed(this, "failed", "Not reachable");
}

void
Assembly::heartbeat_recv(uint32_t timestamp, uint32_t sequence)
{
	gdouble elapsed = 0;

	if (_hb_state != Assembly::HEARTBEAT_OK) {
		_last_sequence = sequence;
		_hb_state = Assembly::HEARTBEAT_OK;
		qb_log(LOG_INFO, "Got the first heartbeat.");
		g_timer_stop(_last_heartbeat);
		g_timer_start(_last_heartbeat);
		return;
	}
	if (sequence > (_last_sequence + 1)) {
		_hb_state = Assembly::HEARTBEAT_SEQ_BAD;
		qb_log(LOG_WARNING, "assembly heartbeat missed a sequence!");
		return;

	} else {
		_last_sequence = sequence;
	}
	g_timer_stop(_last_heartbeat);
	elapsed = g_timer_elapsed(_last_heartbeat, NULL);
	if (elapsed > (5 * 1.5)) {
		_hb_state = Assembly::HEARTBEAT_NOT_RECEIVED;
		qb_log(LOG_WARNING, "assembly heartbeat too late! (%.2f > 5 seconds)",
		       elapsed);
		return;
	}
	g_timer_start(_last_heartbeat);
}

void
Assembly::stop(void)
{
	if (_state > STATE_INIT) {
		_state = STATE_INIT;
	}
	deref();
}

Assembly::Assembly() :
	_hb_state(HEARTBEAT_INIT), _refcount(1), _dep(NULL),
	_name(""), _uuid(""), _state(STATE_OFFLINE)
{
	state_table[STATE_OFFLINE] = &Assembly::check_state_offline;
	state_table[STATE_ONLINE] = &Assembly::check_state_online;
	state_table[STATE_INIT] = NULL;

	state_action_table[STATE_OFFLINE][STATE_ONLINE] = &Assembly::state_offline_to_online;
	state_action_table[STATE_OFFLINE][STATE_OFFLINE] = NULL;
	state_action_table[STATE_ONLINE][STATE_OFFLINE] = &Assembly::state_online_to_offline;
	state_action_table[STATE_ONLINE][STATE_ONLINE] = NULL;

	_last_heartbeat = g_timer_new();
}

Assembly::~Assembly()
{
	qb_log(LOG_DEBUG, "~Assembly(%s)", _name.c_str());
}

Assembly::Assembly(Deployable *dep, std::string& name,
		   std::string& uuid) :
	_hb_state(HEARTBEAT_INIT), _refcount(1), _dep(dep),
	_name(name), _uuid(uuid), _state(STATE_OFFLINE)
{
	state_table[STATE_OFFLINE] = &Assembly::check_state_offline;
	state_table[STATE_ONLINE] = &Assembly::check_state_online;
	state_table[STATE_INIT] = NULL;

	state_action_table[STATE_OFFLINE][STATE_ONLINE] = &Assembly::state_offline_to_online;
	state_action_table[STATE_OFFLINE][STATE_OFFLINE] = NULL;
	state_action_table[STATE_ONLINE][STATE_OFFLINE] = &Assembly::state_online_to_offline;
	state_action_table[STATE_ONLINE][STATE_ONLINE] = NULL;

	qb_log(LOG_DEBUG, "Assembly(%s:%s)", name.c_str(), uuid.c_str());

	_mh_host.query_set("{class:Host, package:org.matahariproject}");
	_mh_host.prop_set("hostname", _name);
	_mh_host.event_handler_set(host_event_handler, this);
	_mh_host.connection_event_handler_set(connection_event_handler, this);
	_dep->qmf_object_add(&_mh_host);

	_mh_serv.query_set("{class:Services, package:org.matahariproject}");
	_mh_serv.prop_set("hostname", _name);
	_mh_serv.method_response_handler_set(service_method_response);
	_mh_serv.connection_event_handler_set(connection_event_handler, this);
	_dep->qmf_object_add(&_mh_serv);

	_last_heartbeat = g_timer_new();

	_refcount++;
}
