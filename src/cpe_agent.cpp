/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors:
 *  Steven Dake <sdake@redhat.com>
 *  Angus Salkeld <asalkeld@redhat.com>
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
#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <string>
#include <iostream>
#include "mainloop.h"

#include "cpe_agent.h"
#include "init-dbus.h"

using namespace std;
using namespace qmf;

int
CpeAgent::console_handler(void)
{
	ConsoleEvent event;

	if (console_session->nextEvent(event)) {
		cout << "event" <<endl;
		if (event.getType() == CONSOLE_AGENT_ADD) {
			string extra;
			cout << "agent added" <<endl;
			if (event.getAgent().getName() == console_session->getConnectedBrokerAgent().getName()) {
				extra = "  [Connected Broker]";
				cout << "Agent Added: " << event.getAgent().getName() << extra << endl;
			}
		}
	}

	return TRUE;
}

int
main(int argc, char **argv)
{
	CpeAgent agent;
	int32_t rc;

	rc = agent.init(argc, argv, "cpe");
	if (rc == 0) {
		agent.run();
	}
	return rc;
}

void
CpeAgent::setup(void)
{
        _cpe = qmf::Data(package.data_cpe);
	agent_session.addData(_cpe, "cpe");

        dbus_init();

#if 0
        console_connection = new qpid::messaging::Connection(url, connectionOptions);
	try {
		console_connection->open();
	} catch (qpid::messaging::TransportFailure& e) {
		qb_log(LOG_ERR, e.what());
		return 0;
	}
        console_session = new ConsoleSession(*console_connection, sessionOptions);
	console_session->setAgentFilter("");

	g_timeout_add(5000,
		host_proxy_timeout,
		this);
#endif
}


gboolean
CpeAgent::event_dispatch(AgentEvent *event)
{
	const string& methodName(event->getMethodName());
	uint32_t rc = 0;
	string name;
	string uuid;

	switch (event->getType()) {
	case qmf::AGENT_METHOD:
		if (methodName == "deployable_start") {
			name = event->getArguments()["name"].asString();
			uuid = event->getArguments()["uuid"].asString();
			
			rc = dep_start(name, uuid);
			
			event->addReturnArgument("rc", rc);

		} else if (methodName == "deployable_stop") {
			name = event->getArguments()["name"].asString();
			uuid = event->getArguments()["uuid"].asString();

			rc = dep_stop(name, uuid);
			
			event->addReturnArgument("rc", rc);
		}
		if (rc == 0) {
			agent_session.methodSuccess(*event);
		} else {
			agent_session.raiseException(*event, "oops");
		}
		break;

	default:	
		break;
	}
	return TRUE;
}

uint32_t
CpeAgent::dep_start(string& dep_name, string& dep_uuid)
{
	int32_t rc = init_job_start("dped", dep_uuid.c_str());

	if (rc == 0) {
		qb_log(LOG_INFO, "started dped instance=%s", dep_uuid.c_str());
		return 0;
	} else {
		errno = -rc;
		qb_perror(LOG_ERR, "Failed to start dped instance=%s", dep_uuid.c_str());
		return -rc;
	}
}

uint32_t
CpeAgent::dep_stop(string& dep_name, string& dep_uuid)
{
	int32_t rc = init_job_stop("dped", dep_uuid.c_str());

	if (rc == 0) {
		qb_log(LOG_INFO, "stopped dped instance=%s", dep_uuid.c_str());
		return 0;
	} else {
		errno = -rc;
		qb_perror(LOG_ERR, "Failed to stop dped instance=%s", dep_uuid.c_str());
		return -rc;
	}
}

