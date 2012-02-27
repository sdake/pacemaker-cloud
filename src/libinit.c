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
#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <qb/qblog.h>
#include "init-dbus.h"

#define SERVICE_PATH_SIZE 512

static DBusConnection *connection;
static bool init_use_systemd;

int
dbus_init(void)
{
	DBusError error;
	char link_name[PATH_MAX + 1];
	ssize_t res;

	init_use_systemd = false;
	/*
	 * According to posix, readlink does not null terminate
	 * the results set in link_name.  As a result, we need to
	 * add room for a null terminator and terminate the string
	 * with a null.  What were they thinking?
	 */
	res = readlink("/sbin/init", link_name, PATH_MAX);
	if (res != -1) {
		link_name[res] = '\0';
		if ((strstr(link_name, "systemd") != NULL)) {
			init_use_systemd = true;
		}
	}

	/* Get a connection to the session bus */
	dbus_error_init(&error);
	connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (!connection) {
		g_warning("Failed to connect to the D-BUS daemon: %s", error.message);
		dbus_error_free(&error);
		return 1;
	}

	/* Set up this connection to work in a GLib event loop */
	dbus_connection_setup_with_g_main(connection, NULL);
	dbus_error_free(&error);

	return 0;
}

void
dbus_fini(void)
{

}

static int
_systemd_init_job(const char *method,
		  const char *name,
		  const char *instance)
{
	int32_t rc = 0;
	DBusMessage *m = NULL;
	DBusMessage *reply = NULL;
	const char  *path;
	const char  *mode = "replace";
	DBusError   error;
	char        service_path[SERVICE_PATH_SIZE];
	char       *inst_name = service_path;

	snprintf(inst_name, SERVICE_PATH_SIZE,
		 "%s@%s.service", name, instance);
	qb_log(LOG_INFO, "%s %s", method, inst_name);

	m = dbus_message_new_method_call("org.freedesktop.systemd1",
					 "/org/freedesktop/systemd1",
					 "org.freedesktop.systemd1.Manager",
					 method);
	if (!m) {
		qb_log(LOG_ERR, "Could not allocate message.");
		rc = -ENOMEM;
		goto finish;
	}

	if (!dbus_message_append_args(m,
				      DBUS_TYPE_STRING, &inst_name,
				      DBUS_TYPE_STRING, &mode,
				      DBUS_TYPE_INVALID)) {
		qb_log(LOG_ERR,"Could not append arguments to message.");
		rc = -ENOMEM;
		goto finish;
	}

	dbus_error_init (&error);
	if (!(reply = dbus_connection_send_with_reply_and_block(connection, m, -1, &error))) {

		qb_log(LOG_ERR,"Failed to issue method call: %s", error.message);
		rc = -EIO;
		goto finish;
	}

	if (!dbus_message_get_args(reply, &error,
				   DBUS_TYPE_OBJECT_PATH, &path,
				   DBUS_TYPE_INVALID)) {
		qb_log(LOG_ERR,"Failed to parse reply: %s", error.message);
		rc = -EIO;
		goto finish;
	}

	rc = 0;

finish:
	dbus_error_free(&error);
	if (m) {
		dbus_message_unref(m);
	}
	if (reply) {
		dbus_message_unref(reply);
	}
	return rc;
}

static int
_upstart_init_job(const char* method,
		  const char* service,
		  const char* instance)
{
	int32_t         rc = 0;
	DBusMessage    *m = NULL;
	DBusMessageIter iter;
	DBusMessage    *reply = NULL;
	DBusError       error;
	DBusMessageIter env_iter;
	char           *env[3];
	size_t          env_i;
	const char     *path;
	const char     *env_element;
	int             wait = TRUE;
	char            service_path[SERVICE_PATH_SIZE];
	char            instance_env[SERVICE_PATH_SIZE];

	snprintf(instance_env, SERVICE_PATH_SIZE, "INSTANCE=%s", instance);
	env[0] = instance_env;
	env[1] = NULL;

	snprintf(service_path, SERVICE_PATH_SIZE, "/com/ubuntu/Upstart/jobs/%s", service);

	/* Construct the method call message. */
	m = dbus_message_new_method_call("com.ubuntu.Upstart",
					 service_path,
					 "com.ubuntu.Upstart0_6.Job",
					 method);

	if (!m) {
		return -ENOMEM;
	}

	dbus_message_iter_init_append(m, &iter);

	/* Marshal an array onto the message */
	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &env_iter)) {
		rc = -ENOMEM;
		goto finish;
	}

	for (env_i = 0; env[env_i]; env_i++) {
		env_element = env[env_i];

		/* Marshal a char * onto the message */
		if (!dbus_message_iter_append_basic(&env_iter, DBUS_TYPE_STRING, &env_element)) {
			dbus_message_iter_abandon_container(&iter, &env_iter);
			rc = -ENOMEM;
			goto finish;
		}
	}

	if (!dbus_message_iter_close_container(&iter, &env_iter)) {
		rc = -ENOMEM;
		goto finish;
	}

	/* Marshal an int onto the message */
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &wait)) {
		rc = -ENOMEM;
		goto finish;
	}

	/* Send the message, and wait for the reply. */
	dbus_error_init (&error);

	reply = dbus_connection_send_with_reply_and_block(connection, m, -1, &error);
	if (!reply) {
		if (dbus_error_has_name(&error, DBUS_ERROR_NO_MEMORY)) {
			rc = -ENOMEM;
			goto finish;
		} else {
			qb_log(LOG_ERR, "%s: %s", error.name, error.message);
			rc = -1;
			goto finish;
		}
	}

	if (strcmp(method, "Start") == 0) {
		/* Only the start returns the instance */
		/* Demarshal a char * from the message */
		if (!dbus_message_get_args(reply, &error,
					   DBUS_TYPE_OBJECT_PATH, &path,
					   DBUS_TYPE_INVALID)) {
			qb_log(LOG_ERR, "Failed to parse reply: %s",
			       error.message);
			rc = -EIO;
		}
	}

finish:
	dbus_error_free(&error);
	if (m) {
		dbus_message_unref(m);
	}
	if (reply) {
		dbus_message_unref(reply);
	}
	return rc;
}

int
init_job_start(const char* service, const char* instance)
{
	assert(service);
	assert(instance);
	if (init_use_systemd) {
		return _systemd_init_job("StartUnit", service, instance);
	} else {
		return _upstart_init_job("Start", service, instance);
	}
}

int
init_job_reload(const char* service, const char* instance)
{
	assert(service);
	assert(instance);
	if (init_use_systemd) {
		return _systemd_init_job("ReloadUnit", service, instance);
	} else {
		return _upstart_init_job("Reload", service, instance);
	}
}

int
init_job_stop(const char * service, const char * instance)
{
	assert(service);
	assert(instance);
	if (init_use_systemd) {
		return _systemd_init_job("StopUnit", service, instance);
	} else {
		return _upstart_init_job("Stop", service, instance);
	}
}

