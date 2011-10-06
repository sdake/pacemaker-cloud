/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Andrew Beekhof <andrew@beekhof.net>
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

extern "C" {
#include "config.h"
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
};
#include <glib.h>
#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <exception>
#include <cstdlib>

#include <qmf/AgentSession.h>
#include <qmf/AgentEvent.h>
#include <qmf/Schema.h>
#include <qmf/SchemaProperty.h>
#include <qmf/SchemaMethod.h>
#include <qmf/Data.h>
#include <qmf/DataAddr.h>
#include <qpid/types/Variant.h>

#include "common_agent.h"

using namespace std;
using namespace qmf;
namespace _qmf = qmf::org::pacemakercloud;

int32_t qpid_level[8] = { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING,
	LOG_ERR, LOG_CRIT };

struct LibqbLogger : public Logger::Output {

	LibqbLogger(Logger& l) {
		l.output(std::auto_ptr<Logger::Output>(this));
	}

	void log(const Statement& s, const string& m) {
		uint8_t priority = qpid_level[s.level];
		assert(priority <= LOG_TRACE);
		qb_log_from_external_source(s.function, s.file, "%s",
					    priority, s.line,
					    1, m.c_str());
	}
};

// Global Variables
Logger& l = Logger::instance();
LibqbLogger* out;

struct option opt[] = {
	{"help", no_argument, NULL, 'h'},
	{"daemon", no_argument, NULL, 'd'},
	{"broker", required_argument, NULL, 'b'},
	{"gssapi", no_argument, NULL, 'g'},
	{"username", required_argument, NULL, 'u'},
	{"password", required_argument, NULL, 'P'},
	{"service", required_argument, NULL, 's'},
	{"port", required_argument, NULL, 'p'},
	{"http-port", required_argument, NULL, 'H'},
	{"conductor-host", required_argument, NULL, 'c'},
	{"conductor-port", required_argument, NULL, 'C'},
	{0, 0, 0, 0}
};

static int32_t
sig_handler(int32_t rsignal, void *data)
{
	CommonAgent *agent = (CommonAgent *)data;
	agent->signal_handler(rsignal);
	return QB_FALSE;
}

static int
get_port (const char* optarg)
{
	int port = 0;

	if (optarg) {
		port = atoi(optarg);
		if (port < 1 || port > 65535)
			port = 0;
	}

	return port;
}

void
CommonAgent::signal_handler(int32_t rsignal)
{
	if (rsignal == SIGTERM) {
		qb_loop_stop(mainloop);
	}
}

static bool
qpid_callback(AgentEvent *event, void* user_data)
{
	CommonAgent *agent = (CommonAgent *)user_data;
	return agent->event_dispatch(event);
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

void
CommonAgent::usage(void)
{
	printf("Usage:\t%sd <options>\n", this->proc_name);
	printf("\t-d | --daemon     run as a daemon\n");
	printf("\t-h | --help       print this help message\n");
	printf("\t-b | --broker     broker host name to connect to (default: %s)\n", this->broker_host.c_str());
	printf("\t-p | --port       broker port to connect to (default: %d)\n", this->broker_port);
	printf("\t-g | --gssapi     force GSSAPI authentication with broker\n");
	printf("\t-u | --username   username to authenticate with broker\n");
	printf("\t-P | --password   password to authenticate with broker\n");
	printf("\t-s | --service    service name to authenticate with broker\n");
	if (this->http_port()) {
		printf("\t-H | --http-port  port for HTTP interface to listen on (default: %d)\n",
		       this->http_port());
	}
	if (this->conductor_host()) {
		printf("\t-c | --conductor  aeolus-conductor host name to connect to (default: %s)\n",
		       this->conductor_host());
	}
	if (this->conductor_port()) {
		printf("\t-C | --conductor-port  aeolus-conductor port to connect to (default: %d)\n",
		       this->conductor_port());
	}
}

void
CommonAgent::unsupported (int arg)
{
	fprintf(stderr, "unsupported option '-%c'\n", arg);
	this->usage();
	exit(0);
}

int
CommonAgent::init(int argc, char **argv, const char *proc_name)
{
	int arg;
	int idx = 0;
	bool daemonize = false;
	int loglevel = LOG_INFO;
	const char* log_argv[]={
		0,
		"--log-to-stderr", "no"
	};
	qpid::types::Variant::Map options;
	std::stringstream url;

	this->proc_name = proc_name;
	qpid::log::Options opts(proc_name);
	opts.parse(sizeof(log_argv)/sizeof(char*), const_cast<char**>(log_argv));
	opts.time = false;
	opts.level = false;

	l.configure(opts);

	log_selector.enable(error);
	log_selector.enable(warning);
	log_selector.enable(info);
//	log_selector.enable(debug);
	l.select(log_selector);

	out = new LibqbLogger(l);

	/* disable glib's fancy allocators that can't be free'd */
	GMemVTable vtable;
	vtable.malloc = malloc;
	vtable.realloc = realloc;
	vtable.free = free;
	vtable.calloc = calloc;
	vtable.try_malloc = malloc;
	vtable.try_realloc = realloc;
	g_mem_set_vtable(&vtable);

	while ((arg = getopt_long(argc, argv, "hdb:gu:P:s:p:vH:c:C:", opt, &idx)) != -1) {
		switch (arg) {
		case 'h':
		case '?':
			this->usage();
			exit(0);
			break;
		case 'd':
			daemonize = true;
			break;
		case 'v':
			loglevel++;
			break;
		case 's':
			if (optarg) {
				this->broker_service = optarg;
			} else {
				this->usage();
				exit(1);
			}
			break;
		case 'u':
			if (optarg) {
				this->broker_username = optarg;
			} else {
				this->usage();
				exit(1);
			}
			break;
		case 'P':
			if (optarg) {
				this->broker_password = optarg;
			} else {
				this->usage();
				exit(1);
			}
			break;
		case 'g':
			this->broker_gssapi = true;
			break;
		case 'p':
			this->broker_port = get_port(optarg);
			if (!this->broker_port) {
				this->usage();
				exit(1);
			}
			break;
		case 'b':
			if (optarg) {
				this->broker_host = optarg;
			} else {
				this->usage();
				exit(1);
			}
			break;
		case 'H':
			if (this->http_port()) {
				int port = get_port(optarg);
				if (port) {
					this->http_port(port);
				} else {
					this->usage();
					exit(1);
				}
			} else {
				this->unsupported(arg);
			}
			break;
		case 'c':
			if (this->conductor_host()) {
				if (optarg) {
					this->conductor_host(optarg);
				} else {
					this->usage();
					exit(1);
				}
			} else {
				this->unsupported(arg);
			}
			break;
		case 'C':
			if (this->conductor_port()) {
				int port = get_port(optarg);
				if (port) {
					this->conductor_port(atoi(optarg));
				} else {
					this->usage();
					exit(1);
				}
			} else {
				this->unsupported(arg);
			}
			break;
		default:
			this->unsupported(arg);
			break;
		}
	}

	while (optind < argc) {
		_non_opt_args.push_back(argv[optind++]);
	}

	if (loglevel > LOG_TRACE) {
		loglevel = LOG_TRACE;
	}
	qb_log_init(proc_name, LOG_DAEMON, loglevel);
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

	if (daemonize) {
		if (daemon(0, 0) < 0) {
			fprintf(stderr, "Error daemonizing: %s\n", strerror(errno));
			exit(1);
		}
	}

	qb_log(LOG_INFO, "Connecting to Qpid broker at %s on port %d",
	       this->broker_host.c_str(), this->broker_port);

	options["reconnect"] = true;
	if (this->broker_username.length() > 0) {
		options["username"] = this->broker_username;
	}
	if (this->broker_password.length() > 0) {
		options["password"] = this->broker_password;
	}
	if (this->broker_service.length() > 0) {
		options["sasl-service"] = this->broker_service;
	}
	if (this->broker_gssapi) {
		options["sasl-mechanism"] = "GSSAPI";
	}

	url << this->broker_host << ":" << this->broker_port;

	agent_connection = qpid::messaging::Connection(url.str(), options);
	agent_connection.open();

	agent_session = AgentSession(agent_connection, "{interval:30}");
	agent_session.setVendor("pacemakercloud.org");
	agent_session.setProduct(proc_name);

	package.configure(agent_session);
	agent_session.open();

	mainloop = qb_loop_create();
	mainloop_default_set(mainloop);

	setup();

	qmf_source = mainloop_add_qmf_session(&agent_session,
					      qpid_callback,
					      this);

	qb_loop_signal_handle sig_handle;
	qb_loop_signal_add(mainloop, QB_LOOP_HIGH,
			   SIGTERM, this,
			   sig_handler,
			   &sig_handle);
	qb_loop_signal_add(mainloop, QB_LOOP_HIGH,
			   SIGHUP, this,
			   sig_handler,
			   &sig_handle);
	return 0;
}

void
CommonAgent::run()
{
	qb_loop_run(mainloop);
}
