/* mh_agent.cpp - Copyright (C) 2010 Red Hat, Inc.
 * Written by Andrew Beekhof <andrew@beekhof.net>
 *
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

#ifndef __COMMON_AGENT_H
#define __COMMON_AGENT_H

#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Duration.h>
#include <qmf/AgentSession.h>
#include <qmf/AgentEvent.h>
#include <qmf/Schema.h>
#include <qmf/SchemaProperty.h>
#include <qmf/SchemaMethod.h>
#include <qmf/Data.h>
#include <qmf/DataAddr.h>
#include <qpid/types/Variant.h>
#include <string>
#include <iostream>

#include <qpid/log/Logger.h>
#include <qpid/log/Options.h>
#include <qpid/log/SinkOptions.h>

extern "C" {
#include "mainloop.h"
}

using namespace std;
using namespace qmf;
using namespace qpid::log;

#include "org/cloudpolicyengine/QmfPackage.h"

class CommonAgent
{
	mainloop_fd_t *qpid_source;
	Selector log_selector;
public:
	GMainLoop *mainloop;
	CommonAgent() {};
	~CommonAgent() {};
	AgentSession agent_session;
	qpid::messaging::Connection agent_connection;
	qmf::org::cloudpolicyengine::PackageDefinition package;

	int init(int argc, char **argv, const char *proc_name);
	void run();
};

#endif
