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

#include <string>
#include <qpid/sys/Time.h>
#include <qpid/agent/ManagementAgent.h>
#include <qpid/management/Manageable.h>
#include <qpid/log/Logger.h>
#include <qpid/log/Options.h>

extern "C" {
#include "mainloop.h"
}

using namespace qpid::management;
using namespace qpid::log;
using namespace std;

#include "org/cloudpolicyengine/Package.h"
namespace _qmf = qmf::org::cloudpolicyengine;

class CommonAgent : public Manageable
{
	mainloop_fd_t *qpid_source;
	Selector log_selector;

public:
	GMainLoop *mainloop;
	CommonAgent() {};
	~CommonAgent() {};

	virtual int setup(ManagementAgent *agent) { return 0; };
	int init(int argc, char **argv, const char* proc_name);
	void run();
};

#endif
