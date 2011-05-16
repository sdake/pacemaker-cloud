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
#include <string.h>
#include <qb/qblog.h>
#include <iostream>
#include <sstream>
#include <map>
#include <assert.h>
#include "pcmk_pe.h"
#include "mainloop.h"
#include "assembly.h"
#include "deployable.h"


using namespace std;
using namespace qmf;

struct pe_operation *
Assembly::op_remove_by_correlator(uint32_t correlator)
{
	struct pe_operation *op = _ops[correlator];
	_ops.erase(correlator);
	return op;
}

gboolean
Assembly::process_qmf_events(void)
{
	uint32_t rc = 0;
	ConsoleEvent event;
	bool got_event = false;
	struct pe_operation *op;

	if (state == Assembly::STATE_INIT) {
		deref();
		return FALSE;
	}

	matahari_discover();
	while (session->nextEvent(event, qpid::messaging::Duration::IMMEDIATE)) {
		if (event.getType() == CONSOLE_EVENT) {
			uint32_t seq;
			uint32_t tstamp;
			const Data& event_data(event.getData(0));

			if (event_data.getSchemaId().getPackageName() != "org.matahariproject" ||
			    event_data.getSchemaId().getName() != "heartbeat") {
				continue;
			}

			tstamp = event_data.getProperty("timestamp");
			seq = event_data.getProperty("sequence");
			heartbeat_recv(tstamp, seq);
			got_event = true;

		} else if (event.getType() == CONSOLE_AGENT_DEL) {
			if (event.getAgentDelReason() == AGENT_DEL_AGED) {
				qb_log(LOG_NOTICE, "CONSOLE_AGENT_DEL (aged) %s",
				       event.getAgent().getName().c_str());
			} else {
				qb_log(LOG_NOTICE, "CONSOLE_AGENT_DEL (filtered) %s",
				       event.getAgent().getName().c_str());
			}
			_dead_agents.remove(event.getAgent().getName());

		} else if (event.getType() == CONSOLE_METHOD_RESPONSE) {
			qpid::types::Variant::Map my_map = event.getArguments();
			op = op_remove_by_correlator(event.getCorrelator());
			rc = pe_resource_ocf_exitcode_get(op, my_map["rc"].asUint32());
			qb_log(LOG_INFO, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%d",
			       op->method, op->rname, op->rclass, op->rtype, op->hostname,
			       op->interval, rc);
			if (op->interval == 0) {
				pe_resource_completed(op, rc);
				pe_resource_unref(op); // delete
			} else if (op->action != NULL) {
				pe_resource_completed(op, rc);
				op->action = NULL;
				op->graph = NULL;
			} else if (rc != op->target_outcome) {

				resource_failed(op);

				// delete request - timer will delete
				// this is only for repeats
				pe_resource_unref(op);
			}
			// remove the ref for putting the op in the map
			pe_resource_unref(op);
		} else if (event.getType() == CONSOLE_EXCEPTION) {
			rc = OCF_UNKNOWN_ERROR;
			op = op_remove_by_correlator(event.getCorrelator());

			if (event.getDataCount() >= 1) {
				string error(event.getData(0).getProperty("error_text"));
				qb_log(LOG_ERR, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%s",
				       op->method, op->rname, op->rclass, op->rtype, op->hostname,
				       op->interval, error.c_str());
			} else {
				qb_log(LOG_ERR, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%d",
				       op->method, op->rname, op->rclass, op->rtype, op->hostname,
				       op->interval, rc);
			}
			pe_resource_completed(op, rc);
			// remove the ref for putting the op in the map
			pe_resource_unref(op);
		}
	}
	check_state();
	return TRUE;
}

static void
_poll_for_qmf_events(gpointer data)
{
	Assembly *a = (Assembly *)data;
	qb_loop_timer_handle timer_handle;

	if (a->process_qmf_events()) {
		mainloop_timer_add(1000, data,
				   _poll_for_qmf_events,
				   &timer_handle);
	}
}

void
Assembly::matahari_discover(void)
{
	Agent a;
	ConsoleEvent ce;
	int32_t ai;
	int32_t ac;
	int32_t q;
	bool dont_use;

	// only search for agents if we have a heartbeat
	if (state != Assembly::STATE_OFFLINE) {
		return;
	}
	if (_mh_serv_class_found && _mh_rsc_class_found) {
		return;
	}
	if (hb_state != HEARTBEAT_OK) {
		return;
	}
	string common = "package:org.matahariproject";
//	common += ", where:[eq, uuid, [quote, " + string(_uuid) + "]]";
	common += "}";

	qb_enter();

	ac = session->getAgentCount();
	qb_log(LOG_DEBUG, "session has %d agents", ac);
	for (ai = 0; ai < ac; ai++) {
		dont_use = false;
		a = session->getAgent(ai);
		if (a.getVendor() != "matahariproject.org" ||
		    a.getProduct() != "service") {
			continue;
		}

		qb_log(LOG_INFO, "looking at agent %s", a.getName().c_str());
		for (list<string>::iterator it = _dead_agents.begin();
		     it != _dead_agents.end(); ++it) {
			if (*it == a.getName()) {
				dont_use = true;
				break;
			}
		}
		if (dont_use) {
			qb_log(LOG_INFO, "*** ignoring dead agent %s",
			       a.getName().c_str());
			continue;
		}

		if (!_mh_serv_class_found) {
			ce = a.query("{class:Services, " + common);
			for (q = 0; q < ce.getDataCount(); q++) {
				qb_log(LOG_INFO, "WOOT found Services class %d of %d",
				       q, ce.getDataCount());
				if (q == 0) {
					qb_log(LOG_INFO, "choosing agent %s", a.getName().c_str());
					_mh_serv_class_found = true;
					_mh_serv_class = ce.getData(0);
				}
			}
		}
		if (!_mh_rsc_class_found) {
			ce = a.query("{class:Resources, " + common);
			if (ce.getDataCount() >= 1) {
				_mh_rsc_class_found = true;
				_mh_rsc_class = ce.getData(0);
				qb_log(LOG_DEBUG, "WOOT found Resources class");
			}
		}
		if (_mh_serv_class_found && _mh_rsc_class_found) {
			break;
		}
	}
	qb_leave();
}

static void
resource_interval_timeout(gpointer data)
{
	struct pe_operation *op = (struct pe_operation *)data;
	Assembly *a = (Assembly *)op->user_data;
	qb_loop_timer_handle timer_handle;

	qb_enter();
	if (op->refcount == 1) {
		// we are the last ref holder
		pe_resource_unref(op);
		return;
	}

	a->_resource_execute(op);
	qb_leave();

	mainloop_timer_add(op->interval, data,
			   resource_interval_timeout,
			   &timer_handle);
}

void
Assembly::resource_failed(struct pe_operation *op)
{
	qb_log(LOG_NOTICE, "resourse %s:%s:%s FAILED",
	       _name.c_str(), op->rname, op->rtype);

	_dep->status_changed();
}

void
Assembly::resource_execute(struct pe_operation *op)
{
	qb_loop_timer_handle timer_handle;

	qb_enter();
	if (state != STATE_ONLINE) {
		qb_log(LOG_DEBUG, "can't execute resourse in offline state");
		if (op->interval > 0 && strcmp(op->method, "monitor") == 0) {
			pe_resource_completed(op, OCF_UNKNOWN_ERROR);
		}
		pe_resource_unref(op); // delete
		return;
	}

	if (op->interval > 0 && strcmp(op->method, "monitor") == 0) {
		op->user_data = this;
		pe_resource_ref(op);
		mainloop_timer_add(op->interval,
				   op, resource_interval_timeout,
				   &timer_handle);
	} else {
		_resource_execute(op);
	}
	qb_leave();
}

void
Assembly::_resource_execute(struct pe_operation *op)
{
	Agent a;
	qpid::types::Variant::Map in_args;
	qpid::types::Variant::Map in_params;
	const char *rmethod = op->method;

	qb_enter();

	if (state != Assembly::STATE_ONLINE) {
		qb_log(LOG_DEBUG, "can't execute resourse in offline state");
		pe_resource_completed(op, OCF_UNKNOWN_ERROR);
		pe_resource_unref(op); // delete
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
		uint32_t corralation_id;
		a = _mh_serv_class.getAgent();
		if (strcmp(op->method, "monitor") == 0) {
			rmethod = "status";
		}
		in_args["name"] = op->rtype;
		pe_resource_ref(op);
		corralation_id = a.callMethodAsync(rmethod, in_args, _mh_serv_class.getAddr());
		qb_log(LOG_DEBUG, "callMethodAsync: %d", corralation_id);
		_ops[corralation_id] = op;
	} else {
		uint32_t corralation_id;
		a = _mh_rsc_class.getAgent();
		// make a non-empty parameters map
		in_params["qmf"] = "frustrating";

		in_args["name"] = op->rname;
		in_args["class"] = op->rclass;
		in_args["provider"] = op->rprovider;
		in_args["type"] = op->rtype;
		in_args["parameters"] = in_params;
		pe_resource_ref(op);
		corralation_id = a.callMethodAsync(rmethod, in_args, _mh_rsc_class.getAddr());
		_ops[corralation_id] = op;
	}
	qb_leave();
}

void
Assembly::deref(void)
{
	refcount--;
	if (refcount == 0) {
		delete this;
	}
}

void
Assembly::insert_status(xmlNode *status)
{
	xmlNode *node_state = xmlNewChild(status, NULL, BAD_CAST "node_state", NULL);

	qb_enter();
	xmlNewProp(node_state, BAD_CAST "id", BAD_CAST _name.c_str());
	xmlNewProp(node_state, BAD_CAST "uname", BAD_CAST _name.c_str());

	xmlNewProp(node_state, BAD_CAST "ha", BAD_CAST "active");
	xmlNewProp(node_state, BAD_CAST "expected", BAD_CAST "member");
	xmlNewProp(node_state, BAD_CAST "in_ccm", BAD_CAST "true");

	if (state == STATE_ONLINE) {
		xmlNewProp(node_state, BAD_CAST "crmd", BAD_CAST "online");
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "member");
	} else {
		xmlNewProp(node_state, BAD_CAST "crmd", BAD_CAST "offline");
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "down");
	}
	qb_leave();
}

void
Assembly::check_state(void)
{
	uint32_t new_state = (this->*state_table[state])();

	if (state_action_table[state][new_state]) {
		(this->*state_action_table[state][new_state])();
	}
	if (state != new_state) {
		state = new_state;
		_dep->status_changed();
	}
}

uint32_t
Assembly::check_state_online(void)
{
	uint32_t new_state = state;
	gdouble elapsed = 0;

	if (hb_state == HEARTBEAT_OK) {
		elapsed = g_timer_elapsed(_last_heartbeat, NULL);
		if (elapsed > (5 * 1.5)) {
			hb_state = Assembly::HEARTBEAT_NOT_RECEIVED;
			qb_log(LOG_WARNING,
			       "assembly heartbeat too late! (%.2f > 5 seconds)",
			       elapsed);
		}
	}
	if (hb_state != HEARTBEAT_OK) {
		new_state = STATE_OFFLINE;
	}
	return new_state;
}

uint32_t
Assembly::check_state_offline(void)
{
	uint32_t new_state = state;
	if (hb_state == HEARTBEAT_OK &&
	    _mh_serv_class_found &&
	    _mh_rsc_class_found) {
		new_state = STATE_ONLINE;
	}
	return new_state;
}

void
Assembly::state_offline_to_online(void)
{
	qb_log(LOG_NOTICE, "Assembly (%s) STATE_ONLINE.",
	       _name.c_str());
}

void
Assembly::state_online_to_offline(void)
{
	map<uint32_t, struct pe_operation*>::iterator iter;
	struct pe_operation *op;

	qb_log(LOG_NOTICE, "Assembly (%s) STATE_OFFLINE.",
	       _name.c_str());
	_dead_agents.push_back(_mh_serv_class.getAgent().getName());
	_dead_agents.push_back(_mh_rsc_class.getAgent().getName());
	// TODO we need a timer -
	// the same as the qmf agent ageing one to
	// remove these agents from the _dead_agents list
	// incase of network down/up (so the agent uuid will
	// be the same)
	_mh_serv_class_found = false;
	_mh_rsc_class_found = false;

	for (iter = _ops.begin(); iter != _ops.end(); iter++) {
		op = iter->second;
		if (op->interval == 0) {
			pe_resource_completed(op, OCF_UNKNOWN_ERROR);
		}
		pe_resource_unref(op); // delete
	}
}

void
Assembly::heartbeat_recv(uint32_t timestamp, uint32_t sequence)
{
	gdouble elapsed = 0;

	qb_enter();
	if (hb_state != Assembly::HEARTBEAT_OK) {
		_last_sequence = sequence;
		hb_state = Assembly::HEARTBEAT_OK;
		qb_log(LOG_INFO, "Got the first heartbeat.");
		g_timer_stop(_last_heartbeat);
		g_timer_start(_last_heartbeat);
		return;
	}
	if (sequence > (_last_sequence + 1)) {
		hb_state = Assembly::HEARTBEAT_SEQ_BAD;
		qb_log(LOG_WARNING, "assembly heartbeat missed a sequence!");
		return;

	} else {
		_last_sequence = sequence;
	}
	g_timer_stop(_last_heartbeat);
	elapsed = g_timer_elapsed(_last_heartbeat, NULL);
	if (elapsed > (5 * 1.5)) {
		hb_state = Assembly::HEARTBEAT_NOT_RECEIVED;
		qb_log(LOG_WARNING, "assembly heartbeat too late! (%.2f > 5 seconds)",
		       elapsed);
		return;
	}
	g_timer_start(_last_heartbeat);
	qb_leave();
}

void
Assembly::stop(void)
{
	if (state > STATE_INIT) {
		session->close();
		connection->close();
		state = STATE_INIT;
	}
	deref();
}

Assembly::Assembly()
{
	_mh_serv_class_found = false;
	_mh_rsc_class_found = false;
	hb_state = HEARTBEAT_INIT;
	state = STATE_INIT;
	refcount = 1;
	session = NULL;
	connection = NULL;
	_name = "";
}

Assembly::~Assembly()
{
	qb_log(LOG_DEBUG, "~Assembly(%s)", _name.c_str());
	if (state > STATE_INIT) {
		session->close();
		connection->close();
	}
}

Assembly::Assembly(Deployable *dep, std::string& name,
		   std::string& uuid, std::string& ipaddr)
{
	qb_loop_timer_handle timer_handle;
	string url("localhost:49000");

	_mh_serv_class_found = false;
	_mh_rsc_class_found = false;
	hb_state = HEARTBEAT_INIT;
	state = STATE_INIT;

	state_table[STATE_OFFLINE] = &Assembly::check_state_offline;
	state_table[STATE_ONLINE] = &Assembly::check_state_online;
	state_table[STATE_INIT] = NULL;

	state_action_table[STATE_OFFLINE][STATE_ONLINE] = &Assembly::state_offline_to_online;
	state_action_table[STATE_OFFLINE][STATE_OFFLINE] = NULL;
	state_action_table[STATE_ONLINE][STATE_OFFLINE] = &Assembly::state_online_to_offline;
	state_action_table[STATE_ONLINE][STATE_ONLINE] = NULL;

	refcount = 1;
	_dep = dep;
	_name = name;
	_uuid = uuid;
	_ipaddr = ipaddr;

	qb_log(LOG_INFO, "Assembly(%s:%s)", name.c_str(), ipaddr.c_str());

	connection = new qpid::messaging::Connection(url, connectionOptions);;
	connection->open();

	session = new ConsoleSession(*connection, "max-agent-age:1");
	session->open();

	qb_log(LOG_INFO, "Assembly(%s:%s) session open", name.c_str(), ipaddr.c_str());

	state = STATE_OFFLINE;

	_last_heartbeat = g_timer_new();
	mainloop_timer_add(1000, this,
			   _poll_for_qmf_events,
			   &timer_handle);
	refcount++;
}


