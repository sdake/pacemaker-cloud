/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * Author: Andrew Beekhof <andrew@beekhof.net>
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
#ifndef COMMON_AGENT_H_DEFINED
#define COMMON_AGENT_H_DEFINED

#include <string>
#include <iostream>

#include <qmf/AgentSession.h>
#include <qmf/AgentEvent.h>

#include <qpid/log/Logger.h>
#include <qpid/log/Options.h>
#include <qpid/log/SinkOptions.h>

#include "mainloop.h"

using namespace std;
using namespace qmf;
using namespace qpid::log;

#include "org/pacemakercloud/QmfPackage.h"

class CommonAgent
{
private:
	mainloop_qmf_session_t *qmf_source;
	Selector log_selector;
	const char* proc_name;

protected:
	list<string> _non_opt_args;
	int http_port_;

public:
	qb_loop_t *mainloop;
	CommonAgent() {};
	~CommonAgent() {};
	AgentSession agent_session;
	qpid::messaging::Connection agent_connection;
	qmf::org::pacemakercloud::PackageDefinition package;

	virtual void setup(void) {};
	virtual bool event_dispatch(AgentEvent *event) { return false; };
	virtual void signal_handler(int32_t rsignal);
	int init(int argc, char **argv, const char *proc_name);
	void run();
	void usage();
	virtual int http_port(void) { return 0; };
	void http_port(int http_port) { this->http_port_ = http_port; };
};

#endif /* COMMON_AGENT_H_DEFINED */
