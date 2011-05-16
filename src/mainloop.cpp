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
#include <stdlib.h>
#include <assert.h>
#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include "mainloop.h"

static qb_loop_t* default_loop = NULL;

void
mainloop_default_set(qb_loop_t* l)
{
	default_loop = l;
}

static void
mainloop_qmf_session_dispatch(void *user_data)
{
	mainloop_qmf_session_t *trig = (mainloop_qmf_session_t *)user_data;
	bool rerun = true;

	if (trig->asession->nextEvent(trig->event,
				      qpid::messaging::Duration::IMMEDIATE)) {

		rerun = trig->dispatch(&trig->event, trig->user_data);
	}
	if (rerun) {
		qb_loop_job_add(default_loop,
				QB_LOOP_MED,
				trig,
				mainloop_qmf_session_dispatch);
	} else {
		free(trig);
	}
}

mainloop_qmf_session_t*
mainloop_add_qmf_session(qmf::AgentSession *asession,
		bool (*dispatch)(qmf::AgentEvent *event, void* userdata),
		void* userdata)
{
	mainloop_qmf_session_t *qmf_source;

	qmf_source = (mainloop_qmf_session_t *)calloc(1, sizeof(mainloop_qmf_session_t));
	assert(qmf_source != NULL);

	qmf_source->dispatch = dispatch;
	qmf_source->user_data = userdata;
	qmf_source->asession = asession;
	
	qb_loop_job_add(default_loop,
			QB_LOOP_MED,
			qmf_source,
			mainloop_qmf_session_dispatch);

	return qmf_source;
}

void
mainloop_job_add(enum qb_loop_priority p,
		 void *userdata,
		 qb_loop_job_dispatch_fn dispatch_fn)
{
	qb_loop_job_add(default_loop,
			p,
			userdata,
			dispatch_fn);
}


int32_t mainloop_timer_add(uint32_t msec_duration,
			   void *data,
			   qb_loop_timer_dispatch_fn dispatch_fn,
			   qb_loop_timer_handle * timer_handle_out)
{
	return qb_loop_timer_add(default_loop, QB_LOOP_MED,
				 msec_duration * QB_TIME_NS_IN_MSEC,
				 data,
				 dispatch_fn,
				 timer_handle_out);
}

