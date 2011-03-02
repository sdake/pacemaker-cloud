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

#include <string>
#include <iostream>

#include "org/cloudpolicyengine/Deployable.h"
#include "org/cloudpolicyengine/ArgsDeployableAssembly_add.h"
#include "org/cloudpolicyengine/ArgsDeployableAssembly_remove.h"
#include "org/cloudpolicyengine/ArgsDeployableAssembly_status.h"
#include "org/cloudpolicyengine/ArgsDeployableAssemblies_list.h"

#include "deployable.h"
#include "assembly.h"

using namespace std;

DeployableAgent::DeployableAgent(ManagementAgent* agent, DpeAgent* parent,
			std::string& name, std::string& uuid)
{
	_mgmtObject = new _qmf::Deployable(agent, this);

	agent->addObject(_mgmtObject);
	_mgmtObject->set_uuid(uuid);
	_mgmtObject->set_name(name);
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
