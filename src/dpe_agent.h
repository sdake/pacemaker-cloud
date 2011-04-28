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

#ifndef _DPE_H_
#define _DPE_H_

#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Agent.h>

#include <qpid/sys/Mutex.h>

#include "org/cloudpolicyengine/QmfPackage.h"
#include "common_agent.h"

class Deployable;

class DpeAgent : public CommonAgent
{
private:
	qmf::Data _dpe;
	qpid::sys::Mutex map_lock;
	std::map<std::string, Deployable*> deployments;
	uint32_t num_deps;
	uint32_t num_ass;

	void update_stats(uint32_t num_deployables, uint32_t num_assemblies);

public:
	uint32_t dep_load(std::string& name, std::string& uuid);
	uint32_t dep_unload(std::string& name, std::string& uuid);

	void setup(void);
	gboolean event_dispatch(AgentEvent *event);
};
#endif /* _DPE_H_ */

