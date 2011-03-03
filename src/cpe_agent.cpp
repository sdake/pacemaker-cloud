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
#include <string>
#include <iostream>
#include "cpe_agent.h"
#include "upstart-dbus.h"

using namespace std;

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

int
CpeAgent::setup(ManagementAgent* agent)
{
	_agent = agent;
	_management_object = new _qmf::Cpe(agent, this);

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

        loop = g_main_loop_new (NULL, FALSE);

	QPID_LOG(debug, "new deployment: " << dep_uuid);

        upstart_init(loop);

        upstart_job_start("dped", (char *)uuid);

        /* Start the event loop */
//      g_main_loop_run (loop);

//      upstart_fini();

/*
        DeployableAgent *child;

	Mutex::ScopedLock _lock(map_lock);

	child = deployments[dep_name];
	if (child != NULL) {
		return Manageable::STATUS_PARAMETER_INVALID;
	}

	child = new DeployableAgent(_agent, dep_name, dep_uuid);


	deployments[dep_name] = child;

	update_stats(num_deps + 1, num_ass);
*/
	return Manageable::STATUS_OK;
}

Manageable::status_t
CpeAgent::dep_stop(string& name, string& uuid)
{
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
