/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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

#include <iostream>
#include <sstream>
#include <map>

#include "matahari.h"

#undef HEALTHCHECK_TIMEOUT
#define HEALTHCHECK_TIMEOUT 20

using namespace std;
using namespace qmf;

class QmfMultiplexer *mux = NULL;

static void
resource_method_response(QmfAsyncRequest* ar,
			qpid::types::Variant::Map out_args,
			enum QmfObject::rpc_result rpc_rc)
{
	enum ocf_exitcode rc;
	struct pe_operation *op;

	if (rpc_rc == QmfObject::RPC_OK) {
		op = (struct pe_operation *)ar->user_data;
		if (out_args.count("rc") > 0) {
			rc = pe_resource_ocf_exitcode_get(op, out_args["rc"].asUint32());
		} else {
			rc = OCF_UNKNOWN_ERROR;
		}

		resource_action_completed(op, rc);
		// remove the ref for putting the op in the map
		pe_resource_unref(op);
	} else {
		rc = OCF_UNKNOWN_ERROR;
		op = (struct pe_operation *)ar->user_data;

		if (out_args.count("error_text") > 0) {
			string error(out_args["error_text"]);
			qb_log(LOG_NOTICE, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%s",
			       op->method, op->rname, op->rclass, op->rtype, op->hostname,
			       op->interval, error.c_str());
		} else {
			qb_log(LOG_NOTICE, "%s'ing: %s [%s:%s] on %s (interval:%d ms) rpc_rc:%d",
			       op->method, op->rname, op->rclass, op->rtype, op->hostname,
			       op->interval, rpc_rc);
		}
		resource_action_completed(op, rc);
	}
}

static void
connection_event_handler(void *user_data)
{
	Matahari *a = (Matahari*)user_data;
	a->check_state();
}

static void
heartbeat_check_tmo(void *data)
{
	Matahari *a = (Matahari*)data;
	qb_loop_timer_handle th;

	a->check_state();
	if (a->state_get() == RECOVER_STATE_RUNNING) {
		qb_loop_timer_add(NULL, QB_LOOP_MED, 4000 * QB_TIME_NS_IN_MSEC,
				  a, heartbeat_check_tmo, &th);
	}
}

static void
host_event_handler(ConsoleEvent &event, void *user_data)
{
	Matahari *a = (Matahari*)user_data;
	const Data& event_data(event.getData(0));

	if (event_data.getSchemaId().getName() == "heartbeat") {
		uint32_t seq = event_data.getProperty("sequence");
		uint32_t tstamp = event_data.getProperty("timestamp");

		a->heartbeat_recv(tstamp, seq);
		a->check_state();
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
Matahari::resource_action(struct pe_operation *op)
{
	Agent a;
	bool is_monitor_op = false;
	qpid::types::Variant::Map in_args;
	qpid::types::Variant::Map in_params;
	const char *rmethod = op->method;
	string node_uuid = op->node_uuid;

	qb_enter();

	if (_state != RECOVER_STATE_RUNNING) {
		qb_log(LOG_DEBUG, "can't execute resource in offline state");
		resource_action_completed(op, OCF_UNKNOWN_ERROR);
		return;
	}

	if (strcmp(op->method, "monitor") == 0) {
		is_monitor_op = true;
	}
	if (strstr(op->rname, op->hostname) == NULL) {
		if (is_monitor_op) {
			resource_action_completed(op, OCF_NOT_RUNNING);
		} else {
			resource_action_completed(op, OCF_UNKNOWN_ERROR);
		}
		qb_leave();
		return;
	}
	in_args["timeout"] = op->timeout;
	qb_log(LOG_DEBUG, "%s setting timeout to %d", op->method, op->timeout);

	if (strcmp(op->rclass, "lsb") == 0) {
		if (is_monitor_op) {
			rmethod = "status";
			in_args["interval"] = 0;
		}
		in_args["name"] = op->rtype;
		pe_resource_ref(op);
		_mh_serv.method_call_async(rmethod, in_args, op, op->timeout);
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
		_mh_rsc.method_call_async(rmethod, in_args, op, op->timeout);
	}
	qb_leave();
}

uint32_t
Matahari::check_state_online(void)
{
	uint32_t new_state = _state;
	gdouble elapsed = 0;

	if (_hb_state == HEARTBEAT_OK) {
		elapsed = g_timer_elapsed(_last_heartbeat, NULL);
		if (elapsed > HEALTHCHECK_TIMEOUT) {
			_hb_state = Matahari::HEARTBEAT_NOT_RECEIVED;
			qb_log(LOG_WARNING,
			       "assembly (%s) heartbeat too late! (%.2f > %d seconds)",
			       _name.c_str(), elapsed, HEALTHCHECK_TIMEOUT);
		}
	}
	if (_hb_state != HEARTBEAT_OK) {
		new_state = RECOVER_STATE_FAILED;
	}
	return new_state;
}

uint32_t
Matahari::check_state_offline(void)
{
	uint32_t new_state = _state;
	if (_hb_state == HEARTBEAT_OK &&
	    _mh_rsc.is_connected() &&
	    _mh_serv.is_connected() &&
	    _mh_host.is_connected()) {
		new_state = RECOVER_STATE_RUNNING;
	}
	return new_state;
}

void
Matahari::state_offline_to_online(void)
{
	qb_loop_timer_handle th;

	recover_state_set(&_node_access->recover, RECOVER_STATE_RUNNING);

	qb_loop_timer_add(NULL, QB_LOOP_MED, 4000 * QB_TIME_NS_IN_MSEC, this,
			  heartbeat_check_tmo, &th);
}

void
Matahari::state_online_to_offline(void)
{
	_mh_rsc.disconnect();
	_mh_serv.disconnect();
	_mh_host.disconnect();

	/* re-init the heartbeat state
	 */
	_hb_state = Matahari::HEARTBEAT_INIT;

	recover_state_set(&_node_access->recover, RECOVER_STATE_FAILED);
}

void
Matahari::heartbeat_recv(uint32_t timestamp, uint32_t sequence)
{
	gdouble elapsed = 0;

	if (_hb_state != Matahari::HEARTBEAT_OK) {
		_last_sequence = sequence;
		_hb_state = Matahari::HEARTBEAT_OK;
		qb_log(LOG_INFO, "Got the first heartbeat.");
		g_timer_stop(_last_heartbeat);
		g_timer_start(_last_heartbeat);
		return;
	}
	if (sequence > (_last_sequence + 1)) {
		_hb_state = Matahari::HEARTBEAT_SEQ_BAD;
		qb_log(LOG_WARNING, "assembly heartbeat missed a sequence!");
		return;

	} else {
		_last_sequence = sequence;
	}
	g_timer_stop(_last_heartbeat);
	elapsed = g_timer_elapsed(_last_heartbeat, NULL);
	if (elapsed > HEALTHCHECK_TIMEOUT) {
		_hb_state = Matahari::HEARTBEAT_NOT_RECEIVED;
		qb_log(LOG_WARNING, "assembly heartbeat too late! (%.2f > %d seconds)",
		       elapsed, HEALTHCHECK_TIMEOUT);
		return;
	}
	g_timer_start(_last_heartbeat);
}

void
Matahari::check_state(void)
{
	uint32_t old_state = _state;
	uint32_t new_state = (this->*state_table[_state])();

	if (old_state != new_state) {
		_state = new_state;
	}

	if (state_action_table[old_state][new_state]) {
		(this->*state_action_table[old_state][new_state])();
	}
}

Matahari::Matahari()
{
	assert(0);
}

Matahari::~Matahari()
{
	qb_log(LOG_DEBUG, "~Matahari(%s)", _name.c_str());
}

Matahari::Matahari(struct assembly* na, QmfMultiplexer *m,
		   std::string& name, std::string& uuid) :
	_node_access(na), _name(name), _uuid(uuid), _mux(m),
	_state(RECOVER_STATE_FAILED), _hb_state(HEARTBEAT_INIT)
{
	state_table[RECOVER_STATE_FAILED] = &Matahari::check_state_offline;
	state_table[RECOVER_STATE_RUNNING] = &Matahari::check_state_online;
	state_table[RECOVER_STATE_UNKNOWN] = NULL;

	state_action_table[RECOVER_STATE_FAILED][RECOVER_STATE_RUNNING] = &Matahari::state_offline_to_online;
	state_action_table[RECOVER_STATE_FAILED][RECOVER_STATE_FAILED] = NULL;

	state_action_table[RECOVER_STATE_RUNNING][RECOVER_STATE_FAILED] = &Matahari::state_online_to_offline;
	state_action_table[RECOVER_STATE_RUNNING][RECOVER_STATE_RUNNING] = NULL;

	qb_log(LOG_DEBUG, "Matahari(%s:%s)", _name.c_str(), _uuid.c_str());

	_mh_host.query_set("{class:Host, package:org.matahariproject}");
	_mh_host.prop_set("uuid", _uuid);
	_mh_host.event_handler_set(host_event_handler, this);
	_mh_host.connection_event_handler_set(connection_event_handler, this);
	_mux->qmf_object_add(&_mh_host);

	_mh_serv.query_set("{class:Services, package:org.matahariproject}");
	_mh_serv.prop_set("uuid", _uuid);
	_mh_serv.method_response_handler_set(resource_method_response);
	_mh_serv.connection_event_handler_set(connection_event_handler, this);
	_mux->qmf_object_add(&_mh_serv);

	_mh_rsc.query_set("{class:Resources, package:org.matahariproject}");
	_mh_rsc.prop_set("uuid", _uuid);
	_mh_rsc.method_response_handler_set(resource_method_response);
	_mh_rsc.connection_event_handler_set(connection_event_handler, this);
	_mux->qmf_object_add(&_mh_rsc);

	_last_heartbeat = g_timer_new();
}


void ta_disconnect(struct assembly *a)
{
}

void
ta_resource_action(struct assembly * a,
		   struct resource *resource,
		   struct pe_operation *op)
{
	Matahari *m = (Matahari *)a->transport_assembly;
	m->resource_action(op);
}

void*
ta_connect(struct assembly * a)
{
	string name(a->name);
	string u(a->uuid);
	if (mux == NULL) {
		mux = new QmfMultiplexer();
		mux->url_set("localhost:49000");
		mux->filter_set("[or, [eq, _vendor, [quote, 'matahariproject.org']], [eq, _vendor, [quote, 'pacemakercloud.org']]]");
		mux->start();
	}
	if (a->transport_assembly == NULL) {
		a->transport_assembly = new Matahari(a, mux, name, u);
	}
	return a->transport_assembly;
}

