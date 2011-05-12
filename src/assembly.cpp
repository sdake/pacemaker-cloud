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

static gboolean
host_proxy_timeout(gpointer data)
{
	Assembly *a = (Assembly *)data;
	ConsoleEvent event;
	bool got_event = false;

	if (!a->is_connected) {
		a->deref();
		return FALSE;
	}

	a->matahari_discover();
	while (a->nextEvent(event)) {
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
			a->check_heartbeat(tstamp, seq);
			got_event = true;
		}
	}
	if (!got_event) {
		a->check_heartbeat();
	}
	return TRUE;
}

void
Assembly::matahari_discover(void)
{
	Agent a;
	ConsoleEvent ce;
	int32_t ai;
	int32_t ac;

	if (_mh_serv_class_found && _mh_rsc_class_found) {
		return;
	}

	ac = session->getAgentCount();
	for (ai = 0; ai < ac; ai++) {
		a = session->getAgent(ai);
		if (a.getVendor() != "matahariproject.org" ||
		    a.getProduct() != "service") {
			continue;
		}
		if (!_mh_serv_class_found) {
			ce = a.query("{class:Services, package:org.matahariproject}");
			if (ce.getDataCount() >= 1) {
				_mh_serv_class_found = true;
				_mh_serv_class = ce.getData(0);
				qb_log(LOG_DEBUG, "WOOT found Services class");
			}
		}
		if (!_mh_rsc_class_found) {
			ce = a.query("{class:Resources, package:org.matahariproject}");
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
}


static gboolean
resource_interval_timeout(gpointer data)
{
	uint32_t rc = OCF_OK;
	struct pe_operation *op = (struct pe_operation *)data;
	Assembly *a = (Assembly *)op->user_data;

	if (a->_resource_execute(op) != OCF_OK) {
		qb_log(LOG_ERR, "resource %s:%s failed",
		       op->rname, op->rtype);

		free(op->hostname);
		free(op->method);
		free(op->rname);
		free(op->rclass);
		free(op->rprovider);
		free(op->rtype);
		free(op);

		a->resource_failed();
		return FALSE;
	}
	return TRUE;
}

void
Assembly::resource_failed(void)
{
	_dep->status_changed();
}

uint32_t
Assembly::resource_execute(struct pe_operation *op)
{
	struct pe_operation *new_op = NULL;

	while (!_mh_serv_class_found || !_mh_rsc_class_found) {
		matahari_discover();
	}

	if (op->interval > 0 && strcmp(op->method, "monitor") == 0) {
		new_op = (struct pe_operation *)malloc(sizeof(struct pe_operation));
		memcpy(new_op, op, sizeof(struct pe_operation));

		new_op->hostname = strdup(op->hostname);
		new_op->method = strdup(op->method);
		new_op->rname = strdup(op->rname);
		new_op->rclass = strdup(op->rclass);
		if (op->rprovider) {
			new_op->rprovider = strdup(op->rprovider);
		} else {
			new_op->rprovider = NULL;
		}
		new_op->rtype = strdup(op->rtype);

		new_op->user_data = this;
		g_timeout_add(op->interval,
			      resource_interval_timeout,
			      new_op);
		return OCF_OK;
	} else {
		return _resource_execute(op);
	}
}

uint32_t
Assembly::_resource_execute(struct pe_operation *op)
{
	ConsoleEvent ce;
	Agent a;
	qpid::types::Variant::Map in_args;
	qpid::types::Variant::Map in_params;
	uint32_t rc = OCF_OK;
	const char *rmethod = op->method;

	if (strcmp(op->method, "monitor") == 0) {
		in_args["interval"] = 0;
	}
	if (op->timeout < 20000) {
		in_args["timeout"] = 20000;
	} else {
		in_args["timeout"] = op->timeout;
	}

	if (strcmp(op->rclass, "lsb") == 0) {
		a = _mh_serv_class.getAgent();
		if (strcmp(op->method, "monitor") == 0) {
			rmethod = "status";
		}
		in_args["name"] = op->rtype;
		ce = a.callMethod(rmethod, in_args, _mh_serv_class.getAddr());
	} else {
		a = _mh_rsc_class.getAgent();
		// make a non-empty parameters map
		in_params["qmf"] = "frustrating";

		in_args["name"] = op->rname;
		in_args["class"] = op->rclass;
		in_args["provider"] = op->rprovider;
		in_args["type"] = op->rtype;
		in_args["parameters"] = in_params;
		ce = a.callMethod(rmethod, in_args, _mh_rsc_class.getAddr());
	}
	if (ce.getType() == CONSOLE_METHOD_RESPONSE) {

		qpid::types::Variant::Map my_map = ce.getArguments();
		rc = my_map["rc"].asUint32();
		rc = pe_get_ocf_exitcode(rmethod, rc);

	} else if (ce.getType() == CONSOLE_EXCEPTION) {
		if (ce.getDataCount() >= 1) {
			string error(ce.getData(0).getProperty("error_text"));
			qb_log(LOG_ERR, "called \"%s %s[%s]\" and got: %s",
			       rmethod, op->rname, op->rtype, error.c_str());
		}
		rc = OCF_UNKNOWN_ERROR;
	}
	qb_log(LOG_INFO, "%s'ing: %s [%s:%s] on %s (interval:%d ms) result:%d",
	       op->method, op->rname, op->rclass, op->rtype, op->hostname,
	       op->interval, rc);

	return rc;
}

bool
Assembly::nextEvent(ConsoleEvent& e)
{
	return session->nextEvent(e);
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

	xmlNewProp(node_state, BAD_CAST "id", BAD_CAST _name.c_str());
	xmlNewProp(node_state, BAD_CAST "uname", BAD_CAST _name.c_str());

	xmlNewProp(node_state, BAD_CAST "ha", BAD_CAST "active");
	xmlNewProp(node_state, BAD_CAST "expected", BAD_CAST "member");
	xmlNewProp(node_state, BAD_CAST "in_ccm", BAD_CAST "true");

	if (state == HEARTBEAT_INIT || state == HEARTBEAT_OK) {
		xmlNewProp(node_state, BAD_CAST "crmd", BAD_CAST "online");
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "member");
	} else {
		xmlNewProp(node_state, BAD_CAST "crmd", BAD_CAST "offline");
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "down");
	}
}

void
Assembly::check_heartbeat(void)
{
	if (state == Assembly::HEARTBEAT_INIT) {
		qb_log(LOG_INFO, "Still waiting for the first heartbeat.");
		return;
	}
	// TODO how long since the last heartbeat?
}

void
Assembly::check_heartbeat(uint32_t timestamp, uint32_t sequence)
{
	int32_t new_state = Assembly::HEARTBEAT_OK;
	gdouble elapsed = 0;

	if (state == Assembly::HEARTBEAT_INIT) {
		_last_sequence = sequence;
		state = Assembly::HEARTBEAT_OK;
		qb_log(LOG_INFO, "Got the first heartbeat.");
		g_timer_start(_last_heartbeat);
		_dep->status_changed();
		return;
	}
	if (sequence > (_last_sequence + 1)) {
		new_state = Assembly::HEARTBEAT_SEQ_BAD;
		qb_log(LOG_CRIT, "assembly heartbeat missed a sequence!");
	} else {
		_last_sequence = sequence;
	}
	g_timer_stop(_last_heartbeat);
	elapsed = g_timer_elapsed(_last_heartbeat, NULL);
	if (elapsed > (5 * 1.5)) {
		new_state = Assembly::HEARTBEAT_NOT_RECEIVED;
		qb_log(LOG_CRIT, "assembly heartbeat too late! (%.2f > 5 seconds)",
		       elapsed);
	} else {
		g_timer_start(_last_heartbeat);
	}
	if (new_state == Assembly::HEARTBEAT_OK) {
		qb_log(LOG_DEBUG, "Heartbeat from %s good.", _name.c_str());
	}
	if (new_state != state) {
		state = new_state;
		qb_log(LOG_NOTICE, "Assembly (%s) state now %d.",
		       _name.c_str(), state);
		_dep->status_changed();
	}
}

void
Assembly::stop(void)
{
	if (is_connected) {
		session->close();
		connection->close();
		is_connected = false;
	}
	deref();
}

Assembly::Assembly()
{
	_mh_serv_class_found = false;
	_mh_rsc_class_found = false;
	is_connected = false;
	refcount = 1;
	session = NULL;
	connection = NULL;
	_name = "";
}

Assembly::~Assembly()
{
	qb_log(LOG_DEBUG, "~Assembly(%s)", _name.c_str());
	if (is_connected) {
		session->close();
		connection->close();
	}
}

Assembly::Assembly(Deployable *dep, std::string& name,
		   std::string& uuid, std::string& ipaddr)
{
	string url("localhost:49000");

	_mh_serv_class_found = false;
	_mh_rsc_class_found = false;
	is_connected = false;
	refcount = 1;
	state = HEARTBEAT_INIT;
	_dep = dep;

	_name = name;
	_uuid = uuid;
	_ipaddr = ipaddr;

	qb_log(LOG_INFO, "Assembly(%s:%s)", name.c_str(), ipaddr.c_str());

	connection = new qpid::messaging::Connection(url, connectionOptions);;
	connection->open();
	qb_log(LOG_INFO, "Assembly(%s:%s) connection open", name.c_str(), ipaddr.c_str());

	session = new ConsoleSession(*connection, sessionOptions);
	session->open();
	qb_log(LOG_INFO, "Assembly(%s:%s) session open", name.c_str(), ipaddr.c_str());

	is_connected = true;

	_last_heartbeat = g_timer_new();
	g_timeout_add(5000,
		      host_proxy_timeout,
		      this);
	refcount++;
}


