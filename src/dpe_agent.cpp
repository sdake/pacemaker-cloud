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
#include <qb/qblog.h>
#include <string>
#include <iostream>
#include "dpe_agent.h"
#include "deployable.h"

using namespace std;

int
main(int argc, char **argv)
{
	DpeAgent agent;
	int32_t rc;

	rc = agent.init(argc, argv, "dpe");
	if (rc == 0) {
		agent.run();
	}
	return rc;
}

void
DpeAgent::check_args(void)
{
	list<string>::iterator it;
	for (it = _non_opt_args.begin(); it != _non_opt_args.end(); it++) {
		dep_load(*it);
	}
}

static void
dpe_agent_check_args(void* data)
{
	DpeAgent *a = (DpeAgent*)data;
	a->check_args();
}

void
DpeAgent::setup(void)
{
	_dpe = qmf::Data(package.data_dpe);

	num_ass = 1;
	num_deps = 1;
	update_stats(0, 0);

	qb_loop_job_add(NULL, QB_LOOP_LOW, this,
			 dpe_agent_check_args);

	agent_session.addData(_dpe, "dpe");
}

void
DpeAgent::signal_handler(int32_t rsignal)
{
	if (rsignal == SIGTERM) {
		for (map<string, Deployable*>::iterator iter = deployments.begin();
		     iter != deployments.end();  iter++) {
			iter->second->stop();
		}
		qb_loop_stop(mainloop);
	} else if (rsignal == SIGHUP) {
		for (map<string, Deployable*>::iterator iter = deployments.begin();
		     iter != deployments.end();  iter++) {
			iter->second->reload();
		}
	}
}

bool
DpeAgent::event_dispatch(AgentEvent *event)
{
	const string& methodName(event->getMethodName());
	uint32_t rc = 0;
	string name;
	string uuid;

	switch (event->getType()) {
	case qmf::AGENT_METHOD:
		if (methodName == "deployable_load") {
			name = event->getArguments()["name"].asString();
			uuid = event->getArguments()["uuid"].asString();

			rc = dep_load(uuid);

			event->addReturnArgument("rc", rc);

		} else if (methodName == "deployable_unload") {
			name = event->getArguments()["name"].asString();
			uuid = event->getArguments()["uuid"].asString();

			rc = dep_unload(uuid);

			event->addReturnArgument("rc", rc);

		} else if (methodName == "deployable_list") {
			::qpid::sys::Mutex::ScopedLock _lock(map_lock);
			::qpid::types::Variant::List d_list;

			for (map<string, Deployable*>::iterator iter = deployments.begin();
			     iter != deployments.end();  iter++) {

				d_list.push_back(iter->second->uuid_get());

				cout << "listing(active) " << iter->first <<
					iter->second->name_get() <<
					", uuid " <<
					iter->second->uuid_get() << endl;
			}
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
	return true;
}

void
DpeAgent::update_stats(uint32_t deployables, uint32_t assemblies)
{
	if (deployables != num_deps) {
		num_deps = deployables;
		_dpe.setProperty("deployables", num_deps);
	}
	if (assemblies != num_ass) {
		num_ass = assemblies;
		_dpe.setProperty("assemblies", num_ass);
	}
}

uint32_t
DpeAgent::dep_load(string& dep_uuid)
{
	Deployable *child;

	::qpid::sys::Mutex::ScopedLock _lock(map_lock);
	qb_log(LOG_INFO, "loading deployment: %s", dep_uuid.c_str());

	child = deployments[dep_uuid];
	if (child != NULL) {
		qb_log(LOG_ERR, "deployment: %s already loaded",
		       dep_uuid.c_str());
		return EEXIST;
	}

	child = new Deployable(dep_uuid, this);

	deployments[dep_uuid] = child;

	update_stats(num_deps + 1, num_ass);

	return 0;
}

uint32_t
DpeAgent::dep_unload(string& uuid)
{
	Deployable *child;

	::qpid::sys::Mutex::ScopedLock _lock(map_lock);

	child = deployments[uuid];

	if (child) {
		qb_log(LOG_INFO, "request to unload %s", uuid.c_str());
		update_stats(num_deps - 1, num_ass);
		deployments.erase(uuid);
		delete child;
	}
	return 0;
}

