/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of cpe.
 *
 * cpe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * cpe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cpe.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef UPSTART_DBUS_H
#define UPSTART_DBUS_H

#include <glib.h>
#include <dbus/dbus.h>


int upstart_init(GMainLoop *loop);
int upstart_job_start(char * service, char * instance);
int upstart_job_stop(char * service, char * instance);
void upstart_fini(void);

#endif /* UPSTART_DBUS_H */
