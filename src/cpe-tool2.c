/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
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

#include <assert.h>
#include <glib.h>
#include <unistd.h>
#include <qb/qblog.h>

#include "init-dbus.h"

int
main (int argc, char **argv)
{
	(void)g_main_loop_new (NULL, FALSE);

	qb_log_init("cpe-tool", LOG_USER, LOG_DEBUG);
	dbus_init();

	init_job_start("dped", "123-456-aaa");
	sleep(5);
	init_job_stop("dped", "123-456-aaa");

	dbus_fini();
	return 0;
}


