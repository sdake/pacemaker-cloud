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

extern gboolean mainloop_signal(int sig, void (*dispatch)(int sig));

extern gboolean mainloop_add_signal(int sig, void (*dispatch)(int sig));

extern gboolean mainloop_destroy_signal(int sig);


typedef struct trigger_s 
{
	GSource source;
	GDestroyNotify dnotify;
	gboolean trigger;
	void *user_data;
	guint id;

} mainloop_trigger_t;

extern mainloop_trigger_t *mainloop_add_trigger(
    int priority, gboolean (*dispatch)(gpointer user_data), gpointer userdata);

extern void mainloop_set_trigger(mainloop_trigger_t* source);

extern gboolean mainloop_destroy_trigger(mainloop_trigger_t* source);



typedef struct mainloop_fd_s 
{
	GSource source;
	GPollFD	gpoll;
	guint id;
	void *user_data;
	GDestroyNotify dnotify;
	gboolean (*dispatch)(int fd, gpointer user_data);

} mainloop_fd_t;

extern mainloop_fd_t *mainloop_add_fd(int priority, int fd,
				  gboolean (*dispatch)(int fd, gpointer userdata),
				  GDestroyNotify notify, gpointer userdata);

extern gboolean mainloop_destroy_fd(mainloop_fd_t* source);



typedef struct mainloop_child_s mainloop_child_t;
struct mainloop_child_s {
	pid_t	  pid;
	char     *desc;
	unsigned  timerid;
	gboolean  timeout;
	void     *privatedata;

	/* Called when a process dies */
	void (*callback)(mainloop_child_t* p, int status, int signo, int exitcode);

};

extern void mainloop_track_children(int priority);

/*
 * Create a new tracked process
 * To track a process group, use -pid
 */
extern void mainloop_add_child(
    pid_t pid, int timeout, const char *desc, void * privatedata,
    void (*callback)(mainloop_child_t* p, int status, int signo, int exitcode));

#endif
