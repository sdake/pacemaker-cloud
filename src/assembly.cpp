/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of cpe.
 *
 * cpe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * cpe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cpe.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <qb/qblog.h>
#include <iostream>
#include <sstream>
#include <map>
#include <assert.h>
#include "mainloop.h"
#include "assembly.h"

using namespace std;
using namespace qmf;


static gboolean host_proxy_timeout(gpointer data)
{
	Assembly *a = (Assembly *)data;
	ConsoleEvent event;
	bool got_event = false;

	if (!a->is_connected) {
		a->deref();
		return FALSE;
	}

	while (a->nextEvent(event)) {
		if (event.getType() == CONSOLE_EVENT) {
			uint32_t seq;
			uint32_t tstamp;
			stringstream ss;
			const Data& event_data(event.getData(0));

			ss << event_data.getSchemaId().getPackageName()
				<< " ["
				<< event_data.getSchemaId().getName()
				<< "] " << endl;
			if (event_data.getSchemaId().getPackageName() != "org.matahariproject" ||
			    event_data.getSchemaId().getName() != "heartbeat") {
				qb_log(LOG_DEBUG, "--> Ignoring event: %s", ss.str().c_str());
				continue;
			}
			qb_log(LOG_INFO, "--> Processing event: %s", ss.str().c_str());

			tstamp = event_data.getProperty("timestamp");
			seq = event_data.getProperty("sequence");
			a->check_heartbeat(tstamp, seq);
			got_event = true;
		}
	}
	if (!got_event) {
		a->check_heartbeat();
	}
	a->matahari_discover();
	return TRUE;
}

void Assembly::matahari_discover(void)
{
	Agent a;
	ConsoleEvent ce;
	int32_t ai;
	int32_t ac;
	qpid::types::Variant::Map in_args;

	if (_mh_serv_class_found) {
		return;
	}

	ac = session->getAgentCount();
	for (ai = 0; ai < ac; ai++) {
		a = session->getAgent(ai);
		qb_log(LOG_DEBUG, "agent: %s", a.getName().c_str());
		if (a.getVendor() == "matahariproject.org" &&
		    a.getProduct() == "service") {
			ce = a.query("{class:Services, package:org.matahariproject}");
			qb_log(LOG_DEBUG, "queried agent: %s (res:%d)",
			       a.getName().c_str(), ce.getDataCount());
			if (ce.getDataCount() >= 1) {
				_mh_serv_class_found = true;
				_mh_serv_class = ce.getData(0);
				qb_log(LOG_DEBUG, "WOOT found service class");
				break;
			}
		}
	}

	if (_mh_serv_class_found) {
		a = _mh_serv_class.getAgent();
		ce = a.callMethod("list", in_args, _mh_serv_class.getAddr());
	}
}

bool Assembly::nextEvent(ConsoleEvent& e)
{
	return session->nextEvent(e);
}

void Assembly::deref(void)
{
	refcount--;
	if (refcount == 0) {
		delete this;
	}
}

void Assembly::check_heartbeat(void)
{
	if (state == Assembly::HEARTBEAT_INIT) {
		qb_log(LOG_INFO, "Still waiting for the first heartbeat.");
		return;
	}
	// TODO how long since the last heartbeat?
}

void Assembly::check_heartbeat(uint32_t timestamp, uint32_t sequence)
{
	if (state == Assembly::HEARTBEAT_INIT) {
		_last_sequence = sequence;
		_last_timestamp = timestamp;
		state = Assembly::HEARTBEAT_OK;
		qb_log(LOG_INFO, "Got the first heartbeat.");
		return;
	}
	if (sequence > (_last_sequence + 1)) {
		state = Assembly::HEARTBEAT_SEQ_BAD;
		qb_log(LOG_CRIT, "assembly heartbeat missed a sequence!");
	}
	if (timestamp > (_last_timestamp + 5000)) {
		state = Assembly::HEARTBEAT_NOT_RECEIVED;
		qb_log(LOG_CRIT, "assembly heartbeat too late! %d %d",
		       _last_timestamp, timestamp);
	}
	_last_sequence = sequence;
	_last_timestamp = timestamp;
	state = Assembly::HEARTBEAT_OK;
}

void Assembly::state_set(uint32_t new_state)
{
	if (new_state == Assembly::HEARTBEAT_NOT_RECEIVED &&
	    state == Assembly::HEARTBEAT_INIT) {
		qb_log(LOG_INFO, "Still waiting for the first heartbeat.");
		return;
	}
	if (new_state == state) {
		return;
	}
	state = new_state;
}

void Assembly::stop(void)
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
	is_connected = false;
	refcount = 1;
	session = NULL;
	connection = NULL;
	name = "";
}


Assembly::~Assembly()
{
	qb_log(LOG_DEBUG, "~Assembly(%s)", name.c_str());
	if (is_connected) {
		session->close();
		connection->close();
	}
}

Assembly::Assembly(std::string& _name, std::string& _uuid, std::string& _ipaddr)
{
	string url("localhost:49000");

	is_connected = false;
	refcount = 1;
	state = HEARTBEAT_INIT;

	name = _name;
	uuid = _uuid;
	ipaddr = _ipaddr;

	qb_log(LOG_INFO, "Assembly(%s:%s)", name.c_str(), ipaddr.c_str());

	connection = new qpid::messaging::Connection(url, connectionOptions);;
	connection->open();
	qb_log(LOG_INFO, "Assembly(%s:%s) connection open", name.c_str(), ipaddr.c_str());

	session = new ConsoleSession(*connection, sessionOptions);
	session->open();
	qb_log(LOG_INFO, "Assembly(%s:%s) session open", name.c_str(), ipaddr.c_str());

	is_connected = true;

	g_timeout_add(3000,
		      host_proxy_timeout,
		      this);
	refcount++;
}


