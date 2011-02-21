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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <assert.h>

#if __linux__
#include <sys/wait.h>
#include <sys/times.h>
#endif

#include "mainloop.h"

static gboolean
mainloop_fd_prepare(GSource* source, gint *timeout)
{
    return FALSE;
}

static gboolean
mainloop_fd_check(GSource* source)
{
    mainloop_fd_t *trig = (mainloop_fd_t*)source;
    if (trig->gpoll.revents) {
	return TRUE;
    }
    return FALSE;
}

static gboolean
mainloop_fd_dispatch(GSource *source, GSourceFunc callback, gpointer userdata)
{
    mainloop_fd_t *trig = (mainloop_fd_t*)source;
    /*
     * Is output now unblocked? 
     *
     * If so, turn off OUTPUT_EVENTS to avoid going into
     * a tight poll(2) loop.
     */
    if (trig->gpoll.revents & G_IO_OUT) {
	trig->gpoll.events &= ~G_IO_OUT;
    }
    
    if (trig->dispatch != NULL
	&& trig->dispatch(trig->gpoll.fd, trig->user_data) == FALSE) {
	g_source_remove_poll(source, &trig->gpoll);
	g_source_unref(source); /* Really? */
	return FALSE;
    }
    return TRUE;
}

static void
mainloop_fd_destroy(GSource* source)
{
    mainloop_fd_t *trig = (mainloop_fd_t*)source;

    if (trig->dnotify) {
	trig->dnotify(trig->user_data);
    }
}
static GSourceFuncs mainloop_fd_funcs = {
    mainloop_fd_prepare,
    mainloop_fd_check,
    mainloop_fd_dispatch,
    mainloop_fd_destroy,
};

mainloop_fd_t*
mainloop_add_fd(int priority, int fd,
		gboolean (*dispatch)(int fd, gpointer userdata),
		GDestroyNotify notify, gpointer userdata)
{
    GSource *source = NULL;
    mainloop_fd_t *fd_source = NULL;
    assert(sizeof(mainloop_fd_t) > sizeof(GSource));
    source = g_source_new(&mainloop_fd_funcs, sizeof(mainloop_fd_t));
    assert(source != NULL);

    fd_source = (mainloop_fd_t*)source;
    fd_source->id = 0;
    fd_source->gpoll.fd = fd;
    fd_source->gpoll.events = G_IO_ERR|G_IO_NVAL|G_IO_IN|G_IO_PRI|G_IO_HUP;
    fd_source->gpoll.revents = 0;

    /*
     * Normally we'd use g_source_set_callback() to specify the dispatch function,
     * But we want to supply the fd too, so we store it in fd_source->dispatch instead
     */
    fd_source->dnotify = notify;
    fd_source->dispatch = dispatch;
    fd_source->user_data = userdata;

    g_source_set_priority(source, priority);
    g_source_set_can_recurse(source, FALSE);
    g_source_add_poll(source, &fd_source->gpoll);
    
    fd_source->id = g_source_attach(source, NULL);
    return fd_source;
}
