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

#ifndef _CPE_H_
#define _CPE_H_

#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Agent.h>

#include <qpid/sys/Mutex.h>

#include "org/pacemakercloud/QmfPackage.h"
#include "common_agent.h"

class CpeAgent : public CommonAgent
{
private:
	qmf::Data _cpe;
        qpid::messaging::Connection *console_connection;
	qmf::ConsoleSession *console_session;

	uint32_t dep_start(std::string& name, std::string& uuid);
	uint32_t dep_stop(std::string& name, std::string& uuid);
	uint32_t dep_reload(std::string& name, std::string& uuid);

public:
	void setup(void);
	bool event_dispatch(AgentEvent *event);
	int console_handler(void);
};
#endif /* _CPE_H_ */

