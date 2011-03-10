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

#ifndef _CPE_H_
#define _CPE_H_

#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Duration.h>
#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Agent.h>
#include <qpid/types/Variant.h>

#include "org/cloudpolicyengine/Cpe.h"
#include "org/cloudpolicyengine/ArgsCpeDeployable_start.h"
#include "org/cloudpolicyengine/ArgsCpeDeployable_stop.h"

#include <qpid/agent/ManagementAgent.h>
#include "common_agent.h"

class CpeAgent : public CommonAgent
{
private:
	ManagementAgent* _agent;
	_qmf::Cpe* _management_object;
	std::string connectionOptions;
	std::string sessionOptions;
        qpid::messaging::Connection *console_connection;
	qmf::ConsoleSession *console_session;

	Mutex map_lock;
	uint32_t num_deps;
	uint32_t num_ass;

	uint32_t dep_start(std::string& name, std::string& uuid);
	uint32_t dep_stop(std::string& name, std::string& uuid);

public:
	int console_handler(void);
	int setup(ManagementAgent* agent);
	ManagementObject* GetManagementObject() const { return _management_object; }
	status_t ManagementMethod(uint32_t method, Args& arguments, string& text);
};
#endif /* _CPE_H_ */

