/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors:
 *  Steven Dake <sdake@redhat.com>
 *  Angus Salkeld <asalkeld@redhat.com>
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
#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Duration.h>
#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Agent.h>
#include <qpid/types/Variant.h>
#include <string>
#include <iostream>
#include "mainloop.h"

#include "cpe_agent.h"
#include "upstart-dbus.h"

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

#ifdef OUTA
/*
int
CpeAgent::setup(ManagementAgent* agent)
{
	_agent = agent;
	_management_object = new _qmf::Cpe(agent, this);
	string connectionOptions;
	string sessionOptions;
	string url("localhost:49000");

	QPID_LOG(info, "setup()");
        console_connection = new qpid::messaging::Connection(url, connectionOptions);
        console_connection->open();

        console_session = new ConsoleSession(*console_connection, sessionOptions);
	console_session->setAgentFilter("");

        upstart_init(mainloop);

	g_timeout_add(5000,
		host_proxy_timeout,
		this);

	agent->addObject(this->_management_object);
	num_ass = 1;
	num_deps = 1;

	return 1;
}

Manageable::status_t
CpeAgent::dep_start(string& dep_name, string& dep_uuid)
{
	GMainLoop *loop;
	const char *uuid;

	uuid = dep_uuid.c_str();

	QPID_LOG(info, "dpe start " << dep_name);
        upstart_job_start("dped", (char *)uuid);

	return Manageable::STATUS_OK;
}

Manageable::status_t
CpeAgent::dep_stop(string& name, string& uuid)
{
	QPID_LOG(info, "dpe stop " << name);
/*
	DeployableAgent *child;

	Mutex::ScopedLock _lock(map_lock);

	child = deployments[name];

	if (child) {
		cout << "request to unstart " << name << endl;
		deployments.erase(name);
		delete child;
	}
*/
	return Manageable::STATUS_OK;
}

Manageable::status_t
CpeAgent::ManagementMethod(uint32_t method, Args& arguments, string& text)
{
	Manageable::status_t rc;
	_qmf::ArgsCpeDeployable_start *start_args;
	_qmf::ArgsCpeDeployable_stop *stop_args;

	switch(method)
	{
	case _qmf::Cpe::METHOD_DEPLOYABLE_START:
		start_args = (_qmf::ArgsCpeDeployable_start*) &arguments;
		rc = dep_start(start_args->i_name, start_args->i_uuid);
		start_args->o_rc = rc;
		break;

	case _qmf::Cpe::METHOD_DEPLOYABLE_STOP:
		stop_args = (_qmf::ArgsCpeDeployable_stop*)&arguments;
		rc = dep_stop(stop_args->i_name, stop_args->i_uuid);
		stop_args->o_rc = rc;
		break;
	default:
		rc = Manageable::STATUS_NOT_IMPLEMENTED;
		break;
	}

	return rc;
}
#endif
