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
//#include "dpe.h"
#include "deployable.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
};

using namespace std;

int
main(int argc, char **argv)
{
	DpeAgent agent;
	int rc = agent.init(argc, argv, "dpe");
	if (rc == 0) {
		agent.run();
	}
	return rc;
}

void
DpeAgent::update_stats(uint32_t deployables, uint32_t assemblies)
{
	if (deployables != num_deps) {
		num_deps = deployables;
		_management_object->set_deployables(num_deps);
	}
	if (assemblies != num_ass) {
		num_ass = assemblies;
		_management_object->set_assemblies(num_ass);
	}
}

int
DpeAgent::setup(ManagementAgent* agent)
{
	_agent = agent;
	_management_object = new _qmf::Dpe(agent, this);

	agent->addObject(this->_management_object);
	num_ass = 1;
	num_deps = 1;
	update_stats(0, 0);

	return 1;
}

Manageable::status_t
DpeAgent::dep_load(std::string& name, std::string& uuid)
{
        DeployableAgent *child = new DeployableAgent(_agent, this,
						     name, uuid);

	deployments[name] = child;

	update_stats(num_deps + 1, num_ass);
	cout << "request to load deployable " << name << endl;
	return Manageable::STATUS_OK;
}

Manageable::status_t
DpeAgent::dep_unload(std::string& name, std::string& uuid)
{
	DeployableAgent *child = deployments[name];
	if (child) {
		cout << "request to unload " << name << endl;
		update_stats(num_deps - 1, num_ass);
		deployments.erase(name);
		delete child;
	}
	return Manageable::STATUS_OK;
}

Manageable::status_t
DpeAgent::ManagementMethod(uint32_t method, Args& arguments, string& text)
{
	Manageable::status_t rc;
	_qmf::ArgsDpeDeployable_load *load_args;
	_qmf::ArgsDpeDeployable_unload *unload_args;
	_qmf::ArgsDpeDeployables_list *list_args;

	switch(method)
	{
	case _qmf::Dpe::METHOD_DEPLOYABLE_LOAD:
		load_args = (_qmf::ArgsDpeDeployable_load*) &arguments;
		rc = dep_load(load_args->i_name, load_args->i_uuid);
		load_args->o_rc = rc;
		break;

	case _qmf::Dpe::METHOD_DEPLOYABLE_UNLOAD:
		unload_args = (_qmf::ArgsDpeDeployable_unload*)&arguments;
		rc = dep_unload(unload_args->i_name, unload_args->i_uuid);
		unload_args->o_rc = rc;
		break;

	case _qmf::Dpe::METHOD_DEPLOYABLES_LIST:
		list_args = (_qmf::ArgsDpeDeployables_list*)&arguments;
		cout << "request for listing " << endl;

		for (map<string, DeployableAgent*>::iterator iter = deployments.begin();
		     iter != deployments.end();  iter++) {
			list_args->o_deployables.push_back(iter->second->getKey());
		}

		break;

	default:
		rc = Manageable::STATUS_NOT_IMPLEMENTED;
		break;
	}

	return rc;
}
