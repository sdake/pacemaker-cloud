/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Authors: Steven Dake <sdake@redhat.com>
 *          Angus Salkeld <asalkeld@redhat.com>
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
#include <glib.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <assert.h>

#include "cape.h"


static const char *my_tags_stringify(uint32_t tags)
{
	if (qb_bit_is_set(tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
		return "QB   ";
	} else if (tags == 1) {
		return "QPID ";
	} else if (tags == 2) {
		return "GLIB ";
	} else if (tags == 3) {
		return "PCMK ";
	} else {
		return "MAIN ";
	}
}

static void
my_glib_handler(const gchar *log_domain, GLogLevelFlags flags, const gchar *message, gpointer user_data)
{
	uint32_t log_level = LOG_WARNING;
	GLogLevelFlags msg_level = (GLogLevelFlags)(flags & G_LOG_LEVEL_MASK);

	switch (msg_level) {
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_FLAG_FATAL:
		log_level = LOG_CRIT;
		break;
	case G_LOG_LEVEL_ERROR:
		log_level = LOG_ERR;
		break;
	case G_LOG_LEVEL_MESSAGE:
		log_level = LOG_NOTICE;
		break;
	case G_LOG_LEVEL_INFO:
		log_level = LOG_INFO;
		break;
	case G_LOG_LEVEL_DEBUG:
		log_level = LOG_DEBUG;
		break;

	case G_LOG_LEVEL_WARNING:
	case G_LOG_FLAG_RECURSION:
	case G_LOG_LEVEL_MASK:
		log_level = LOG_WARNING;
		break;
	}

	qb_log_from_external_source(__FUNCTION__, __FILE__, "%s",
				    log_level, __LINE__,
				    2, message);
}

int
main(int argc, char * argv[])
{
	int daemonize = 0;
	int loglevel = LOG_INFO;
	qb_loop_t *loop = qb_loop_create();

	qb_log_init(argv[0], LOG_DAEMON, loglevel);
	qb_log_format_set(QB_LOG_SYSLOG, "%g[%p] %b");
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, LOG_INFO - loglevel);
	if (!daemonize) {
		qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
				  QB_LOG_FILTER_FILE, "*", loglevel);
		qb_log_format_set(QB_LOG_STDERR, "%g[%p] %b");
		qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	}
	qb_log_tags_stringify_fn_set(my_tags_stringify);
	g_log_set_default_handler(my_glib_handler, NULL);

	cape_init();
	cape_load();

	qb_loop_run(loop);
	return 0;
}
