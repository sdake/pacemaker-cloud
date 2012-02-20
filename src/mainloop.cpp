/*
 * Copyright (C) 2009 Andrew Beekhof <andrew@beekhof.net>
 * Copyright (C) 2011 Angus Salkeldf <asalkeld@redhat.com>
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
#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <sys/epoll.h>
#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>
#include "mainloop.h"

static int
_poll_for_qmf_events(int32_t fd, int32_t revents, void *user_data)
{
	mainloop_qmf_session_t *trig = (mainloop_qmf_session_t *)user_data;
	bool rerun = true;
	qb_loop_timer_handle th;

	if (trig->asession->nextEvent(trig->event,
				      qpid::messaging::Duration::IMMEDIATE)) {

		rerun = trig->dispatch(&trig->event, trig->user_data);
	}
	if (rerun == 0) {
		free(trig);
	}
}

mainloop_qmf_session_t*
mainloop_add_qmf_session(qmf::AgentSession *asession,
		bool (*dispatch)(qmf::AgentEvent *event, void* userdata),
		void* userdata)
{
	mainloop_qmf_session_t *qmf_source;
        qmf::posix::EventNotifier *event_notifier;


	qmf_source = (mainloop_qmf_session_t *)calloc(1, sizeof(mainloop_qmf_session_t));
	assert(qmf_source != NULL);

	qmf_source->dispatch = dispatch;
	qmf_source->user_data = userdata;
	qmf_source->asession = asession;
	event_notifier = new qmf::posix::EventNotifier(*asession);

	qb_loop_poll_add(NULL, QB_LOOP_MED,
			 event_notifier->getHandle(), EPOLLIN, qmf_source,
			 _poll_for_qmf_events);

	return qmf_source;
}

bool
mainloop_timer_is_running(qb_loop_timer_handle timer_handle)
{
	return (qb_loop_timer_expire_time_get(NULL, timer_handle) > 0);
}

