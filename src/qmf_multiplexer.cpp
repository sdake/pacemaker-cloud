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
#include <uuid/uuid.h>

#include <string>
#include <map>

#include <qmf/DataAddr.h>

#include "mainloop.h"
#include "qmf_agent.h"
#include "qmf_multiplexer.h"

using namespace std;
using namespace qmf;

bool
QmfMultiplexer::process_events(void)
{
	uint32_t rc = 0;
	ConsoleEvent event;
	QmfAgent *qa;
	map<string, QmfObject*>::iterator iter;

	while (session->nextEvent(event, qpid::messaging::Duration::IMMEDIATE)) {
		Agent a = event.getAgent();
		if (event.getType() == CONSOLE_AGENT_ADD) {
			for (list<QmfObject*>::iterator it = _objects.begin();
			     it != _objects.end(); ++it) {
				if ((*it)->connect(a)) {
					qa = _agents[a.getName()];
					if (qa == NULL) {
						qa = new QmfAgent(a);
						_agents[a.getName()] = qa;
					}
					if (qa) {
						qa->add(*it);
					}
				}
			}
		} else 	if (event.getType() == CONSOLE_AGENT_DEL) {
			qa = _agents[a.getName()];
			_agents.erase(a.getName());
			delete qa;
		} else {
			qa = _agents[a.getName()];
			if (qa) {
				qa->process_event(event);
			}
		}
	}
	return true;
}

static void
_poll_for_qmf_events(void* data)
{
	QmfMultiplexer *m = (QmfMultiplexer *)data;
	qb_loop_timer_handle timer_handle;

	if (m->process_events()) {
		mainloop_timer_add(1000, data,
				   _poll_for_qmf_events,
				   &timer_handle);
	}
}

void
QmfMultiplexer::qmf_object_add(QmfObject *qc)
{
	_objects.push_back(qc);
}

void
QmfMultiplexer::start(void)
{
	qb_loop_timer_handle timer_handle;

	connection = new qpid::messaging::Connection(_url, "");
	connection->open();

	session = new ConsoleSession(*connection, "max-agent-age:1");
	session->setAgentFilter(_filter);
	session->open();

	qb_log(LOG_DEBUG, "session to %s open [filter:%s",
	       _url.c_str(), _filter.c_str());

	mainloop_timer_add(1000, this,
			   _poll_for_qmf_events,
			   &timer_handle);
}

