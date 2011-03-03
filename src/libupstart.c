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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "upstart-dbus.h"

#define DBUS_SERVICE_UPSTART                "com.ubuntu.Upstart"
#define DBUS_PATH_UPSTART                  "/com/ubuntu/Upstart"
#define DBUS_INTERFACE_UPSTART              "com.ubuntu.Upstart0_6"
#define DBUS_INTERFACE_UPSTART_JOB          "com.ubuntu.Upstart0_6.Job"
#define DBUS_INTERFACE_UPSTART_INSTANCE     "com.ubuntu.Upstart0_6.Instance"
#define DBUS_ADDRESS_UPSTART "unix:abstract=/com/ubuntu/upstart"

static DBusConnection *connection;

int
upstart_init (GMainLoop *loop)
{
	DBusError error;

	/* Get a connection to the session bus */
	dbus_error_init (&error);
	connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (!connection) {
		g_warning ("Failed to connect to the D-BUS daemon: %s", error.message);
		dbus_error_free (&error);
		return 1;
	}

	/* Set up this connection to work in a GLib event loop */
	dbus_connection_setup_with_g_main(connection, NULL);
	dbus_error_free (&error);

	return 0;
}

int
upstart_job_start(const char* service, const char* instance)
{
	DBusMessage *   method_call;
	DBusMessageIter iter;
	DBusError       error;
	DBusMessage *   reply;
	DBusMessageIter env_iter;
	char *          instance_local;
	const char *    instance_local_dbus;
	char * env[3];
	size_t env_i;
	const char *env_element;
	int wait = TRUE;
	char service_path[512];
	char instance_env[512];

	snprintf(instance_env, 512, "INSTANCE=%s", instance);
	env[0] = instance_env;
	env[1] = NULL;

	snprintf(service_path, 512, "/com/ubuntu/Upstart/jobs/%s", service);

	/* Construct the method call message. */
	method_call = dbus_message_new_method_call(DBUS_SERVICE_UPSTART,
						   service_path,
						   DBUS_INTERFACE_UPSTART_JOB,
						   "Start");
	if (!method_call) {
		return -ENOMEM;
	}

	dbus_message_iter_init_append(method_call, &iter);

	/* Marshal an array onto the message */
	if (! dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &env_iter)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	for (env_i = 0; env[env_i]; env_i++) {
		env_element = env[env_i];

		/* Marshal a char * onto the message */
		if (! dbus_message_iter_append_basic(&env_iter, DBUS_TYPE_STRING, &env_element)) {
			dbus_message_iter_abandon_container(&iter, &env_iter);
			dbus_message_unref(method_call);
			return -ENOMEM;
		}
	}

	if (! dbus_message_iter_close_container(&iter, &env_iter)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	/* Marshal an int onto the message */
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &wait)) {
		dbus_message_unref (method_call);
		return -ENOMEM;
	}

	/* Send the message, and wait for the reply. */
	dbus_error_init (&error);

	reply = dbus_connection_send_with_reply_and_block(connection, method_call, -1, &error);
	if (!reply) {
		dbus_message_unref(method_call);

		if (dbus_error_has_name(&error, DBUS_ERROR_NO_MEMORY)) {
			return -ENOMEM;
		} else {
			printf("%s: %s", error.name, error.message);
			return -1;
		}

		dbus_error_free(&error);
		return -1;
	}

	dbus_message_unref(method_call);

	/* Iterate the arguments of the reply */
	dbus_message_iter_init(reply, &iter);

	do {
		/* Demarshal a char * from the message */
		if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
			dbus_message_unref(reply);
			return -EINVAL;
		}

		dbus_message_iter_get_basic (&iter, &instance_local_dbus);

		instance_local = strdup(instance_local_dbus);
		if (!instance_local) {
			return -ENOMEM;
		}

		dbus_message_iter_next (&iter);
	} while (! *instance);

	if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {
		free (instance_local);
		dbus_message_unref (reply);
		return -EINVAL;
	}

	dbus_message_unref (reply);

	return 0;
}


