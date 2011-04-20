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

#include <glib.h>
#include <sys/types.h>

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <exception>
#include <cstdlib>

#include <qmf/AgentSession.h>
#include <qmf/AgentEvent.h>

typedef struct mainloop_fd_s
{
	GSource source;
	GPollFD	gpoll;
	guint id;
	void *user_data;
	GDestroyNotify dnotify;
	gboolean (*dispatch)(int fd, gpointer user_data);

} mainloop_fd_t;


typedef struct mainloop_qmf_session_s
{
	GSource source;
	guint id;
	void *user_data;
	GDestroyNotify dnotify;
	qmf::AgentEvent event;
	qmf::AgentSession *asession;
	gboolean ready;
	gboolean (*dispatch)(qmf::AgentEvent *event, gpointer user_data);

} mainloop_qmf_session_t;


mainloop_fd_t *mainloop_add_fd(int priority, int fd,
			       gboolean (*dispatch)(int fd, gpointer userdata),
			       GDestroyNotify notify, gpointer userdata);

gboolean mainloop_destroy_fd(mainloop_fd_t* source);

mainloop_qmf_session_t*
mainloop_add_qmf_session(qmf::AgentSession *asession,
			 gboolean (*dispatch)(qmf::AgentEvent *event, gpointer userdata),
			 GDestroyNotify notify, gpointer userdata);

#endif
