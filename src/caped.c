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
#include <pacemaker/crm_config.h>

#include "cape.h"

#define LOG_TAG_QPID 1
#define LOG_TAG_GLIB 2
#define LOG_TAG_PCMK 3

static const char *my_tags_stringify(uint32_t tags)
{
	if (qb_bit_is_set(tags, QB_LOG_TAG_LIBQB_MSG_BIT)) {
		return "QB   ";
	} else if (tags == LOG_TAG_QPID) {
		return "QPID ";
	} else if (tags == LOG_TAG_GLIB) {
		return "GLIB ";
	} else if (tags == LOG_TAG_PCMK) {
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
				    LOG_TAG_GLIB, message);
}

static void
show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options> [cloud app name]\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -v             verbose\n");
	printf("  -o             log to stdout\n");
	printf("  -h             show this help text\n");
	printf("\n");
}

int32_t signal_int(int32_t rsignal, void *data) {
	cape_exit();
	qb_loop_stop(NULL);
	return 0;
}

int
main(int argc, char * argv[])
{
	const char *options = "vhod";
	int32_t opt;
	int32_t do_stdout = QB_FALSE;
	int daemonize = 0;
	int loglevel = LOG_INFO;
	qb_loop_t *loop;
	char *cloud_app = NULL;
	char *prog_name = strrchr(argv[0], '/');

	if (prog_name) {
		prog_name++;
	} else {
		prog_name = argv[0];
	}

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'd':
			daemonize = QB_TRUE;
			break;
		case 'o':
			do_stdout = QB_TRUE;
			break;
		case 'v':
			loglevel++;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}

	if (optind < argc) {
		cloud_app =  argv[optind];
	} else {
		show_usage(prog_name);
		exit(EXIT_FAILURE);
	}

	qb_log_init(prog_name, LOG_DAEMON, loglevel);
	qb_log_format_set(QB_LOG_SYSLOG, "%g[%p] %b");
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_PRIORITY_BUMP, LOG_INFO - loglevel);
	if (!daemonize || do_stdout) {
		qb_log_filter_ctl(QB_LOG_STDOUT, QB_LOG_FILTER_ADD,
				  QB_LOG_FILTER_FILE, "*", loglevel);
		qb_log_format_set(QB_LOG_STDOUT, "%g[%6p] %b");
		qb_log_ctl(QB_LOG_STDOUT, QB_LOG_CONF_ENABLED, QB_TRUE);
	}
	pe_log_init(LOG_TAG_PCMK, loglevel);
	qb_log_tags_stringify_fn_set(my_tags_stringify);
	g_log_set_default_handler(my_glib_handler, NULL);

	qb_enter();
	loop = qb_loop_create();

	qb_loop_signal_add(NULL, QB_LOOP_LOW, SIGINT, NULL, signal_int, NULL);

	cape_init();

	cape_admin_init();

	if (cape_load(cloud_app) != 0) {
		qb_perror(LOG_ERR, "failed to load the configuration");
		qb_log_fini();
		exit(EXIT_FAILURE);
	}

	qb_loop_run(loop);

	cape_admin_fini();

	qb_leave();

	return 0;
}
