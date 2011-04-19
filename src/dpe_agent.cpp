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
#include <string>
#include <iostream>
#include "dpe_agent.h"
#include "deployable.h"

using namespace std;

int
main(int argc, char **argv)
{
	int rc;
	DpeAgent agent;

	rc = agent.init(argc, argv, "dpe");
	if (rc == 0) {
		agent.run();
	}
	return rc;
}

#ifdef OUTA
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
DpeAgent::dep_load(string& dep_name, string& dep_uuid)
{
        Deployable *child;

	Mutex::ScopedLock _lock(map_lock);
	qb_log(LOG_DEBUG, "loading deployment: %s:%s",
	       dep_name.c_str(), dep_uuid.c_str());

	child = deployments[dep_uuid];
	if (child != NULL) {
		qb_log(LOG_ERR, "deployment: %s already loaded",
		       dep_uuid.c_str());
		return Manageable::STATUS_PARAMETER_INVALID;
	}

	child = new Deployable(dep_uuid);

	deployments[dep_uuid] = child;

	update_stats(num_deps + 1, num_ass);

	return Manageable::STATUS_OK;
}

Manageable::status_t
DpeAgent::dep_unload(string& name, string& uuid)
{
	Deployable *child;

	Mutex::ScopedLock _lock(map_lock);

	child = deployments[uuid];

	if (child) {
		qb_log(LOG_DEBUG, "request to unload %s", name.c_str());
		update_stats(num_deps - 1, num_ass);
		deployments.erase(uuid);
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
		{
			Mutex::ScopedLock _lock(map_lock);

			for (map<string, Deployable*>::iterator iter = deployments.begin();
			     iter != deployments.end();  iter++) {

				cout << "listing(active) " << iter->first <<
					iter->second->get_name() <<
					", uuid " <<
					iter->second->get_uuid() << endl;

				list_args->o_deployables.push_back(iter->second->get_uuid());
			}

			break;
		}
	default:
		rc = Manageable::STATUS_NOT_IMPLEMENTED;
		break;
	}

	return rc;
}
#endif
