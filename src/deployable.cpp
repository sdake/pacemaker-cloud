/* Copyright (C) 2010 Red Hat, Inc.
 * Written by Adam Stokes <astokes@fedoraproject.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include <string>
#include <iostream>

#include "org/cloudpolicyengine/Deployable.h"
#include "org/cloudpolicyengine/ArgsDeployableAssembly_add.h"
#include "org/cloudpolicyengine/ArgsDeployableAssembly_remove.h"
#include "org/cloudpolicyengine/ArgsDeployableAssembly_status.h"
#include "org/cloudpolicyengine/ArgsDeployableAssemblies_list.h"

#include <qpid/agent/ManagementAgent.h>
#include "agent.h"
#include "assembly.h"

using namespace std;

extern "C" {
#include <stdlib.h>
#include <string.h>
};

class DeployableAgent : public CpeAgent
{
private:
	ManagementAgent* _agent;
	_qmf::Deployable* _management_object;

public:
	int setup(ManagementAgent* agent);
	ManagementObject* GetManagementObject() const { return _management_object; }
	status_t ManagementMethod(uint32_t method, Args& arguments, string& text);
};

int
main(int argc, char **argv)
{
	DeployableAgent agent;
	int rc = agent.init(argc, argv, "deployable");
	if (rc == 0) {
		agent.run();
	}
	return rc;
}


int
DeployableAgent::setup(ManagementAgent* agent)
{
	this->_agent = agent;
	this->_management_object = new _qmf::Deployable(agent, this);

	agent->addObject(this->_management_object);

	// TODO get the proper values of these
	_management_object->set_uuid("~!@#$%^&*()");
	_management_object->set_name("deplorable deployable");

	return 1;
}

Manageable::status_t
DeployableAgent::ManagementMethod(uint32_t method, Args& arguments, string& text)
{
	Manageable::status_t rc = Manageable::STATUS_NOT_IMPLEMENTED;
	std::string url;

	switch(method)
	{
	case _qmf::Deployable::METHOD_ASSEMBLY_ADD:
		{
			_qmf::ArgsDeployableAssembly_add& ioArgs = (_qmf::ArgsDeployableAssembly_add&) arguments;
			cout << "request to add " << ioArgs.i_name << endl;
			url = ioArgs.i_name;
			url += ":49000";
			if (assembly_monitor_start(url) == 0) {
				rc = Manageable::STATUS_OK;
			} else {
				rc = Manageable::STATUS_PARAMETER_INVALID;
			}
			ioArgs.o_rc = rc;
		}
		break;

	case _qmf::Deployable::METHOD_ASSEMBLY_REMOVE:
		{
			_qmf::ArgsDeployableAssembly_remove& ioArgs = (_qmf::ArgsDeployableAssembly_remove&) arguments;
			url = ioArgs.i_name;
			url += ":49000";
			cout << "request to delete " << ioArgs.i_name << endl;
			if (assembly_monitor_stop(url) == 0) {
				rc = Manageable::STATUS_OK;
			} else {
				rc = Manageable::STATUS_PARAMETER_INVALID;
			}
			ioArgs.o_rc = rc;
		}
		break;

	case _qmf::Deployable::METHOD_ASSEMBLY_STATUS:
		{
			_qmf::ArgsDeployableAssembly_status& ioArgs = (_qmf::ArgsDeployableAssembly_status&) arguments;
			url = ioArgs.i_name;
			url += ":49000";
			cout << "request for status " << ioArgs.i_name << endl;
			if (assembly_monitor_status(url) == 0) {
				rc = Manageable::STATUS_OK;
			} else {
				rc = Manageable::STATUS_PARAMETER_INVALID;
			}
			ioArgs.o_rc = rc;
		}
		break;
	}

	return rc;
}
