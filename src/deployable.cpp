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
#include <iostream>

#include "org/cloudpolicyengine/Deployable.h"
#include "org/cloudpolicyengine/ArgsDeployableHost_add.h"
#include "org/cloudpolicyengine/ArgsDeployableHost_del.h"

#include <qpid/agent/ManagementAgent.h>
#include "agent.h"

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
    int rc = agent.init(argc, argv, "net");
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
//    this->_management_object->set_hostname(get_hostname());

    agent->addObject(this->_management_object);
    return 0;
}

Manageable::status_t
DeployableAgent::ManagementMethod(uint32_t method, Args& arguments, string& text)
{
	Manageable::status_t rc = Manageable::STATUS_NOT_IMPLEMENTED;

	switch(method)
	{
	case _qmf::Deployable::METHOD_HOST_ADD:
		{
		_qmf::ArgsDeployableHost_add& ioArgs = (_qmf::ArgsDeployableHost_add&) arguments;
		cout << "request to add " << ioArgs.i_name << endl;
		rc = Manageable::STATUS_OK;
		break;
		}

	case _qmf::Deployable::METHOD_HOST_DEL:
		{
		_qmf::ArgsDeployableHost_del& ioArgs = (_qmf::ArgsDeployableHost_del&) arguments;
		cout << "request to delete " << ioArgs.i_name << endl;
		rc = Manageable::STATUS_OK;
		break;
		}
	}

    return rc;
}
