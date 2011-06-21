/*
 * Copyright (C) 2009 Andrew Beekhof <andrew@beekhof.net>
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
#ifndef __MH_MAINLOOP__
#define __MH_MAINLOOP__

#include <stdbool.h>
#include <qb/qbloop.h>

#ifdef __cplusplus

#include <qmf/AgentSession.h>
#include <qmf/AgentEvent.h>

typedef struct mainloop_qmf_session_s
{
	void *user_data;
	qmf::AgentEvent event;
	qmf::AgentSession *asession;
	bool (*dispatch)(qmf::AgentEvent *event, void* user_data);
} mainloop_qmf_session_t;

mainloop_qmf_session_t*
mainloop_add_qmf_session(qmf::AgentSession *asession,
			 bool (*dispatch)(qmf::AgentEvent *event, void* userdata),
			 void* user_data);
#endif

#ifdef __cplusplus
extern "C" {
#endif
void mainloop_default_set(qb_loop_t* l);
void mainloop_job_add(enum qb_loop_priority p,
		      void *userdata,
		      qb_loop_job_dispatch_fn dispatch_fn);

int32_t mainloop_timer_add(uint32_t msec_duration,
			  void *data,
			  qb_loop_timer_dispatch_fn dispatch_fn,
			  qb_loop_timer_handle * timer_handle_out);

int32_t mainloop_timer_del(qb_loop_timer_handle th);

bool mainloop_timer_is_running(qb_loop_timer_handle timer_handle);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif

