/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Andrew Beekhof <andrew@beekhof.net>
 *          Angus Salkeld <asalkeld@redhat.com>
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

#include "config.h"
#include <getopt.h>
int use_stderr = 0;

#include <iostream>
#include <fstream>
#include <string.h>
#include <errno.h>
#include <vector>
#include <exception>

#include <signal.h>
#include <syslog.h>
#include <cstdlib>

#include <qpid/sys/Time.h>
#include <qpid/agent/ManagementAgent.h>
#include <qpid/client/ConnectionSettings.h>

#include "org/cloudpolicyengine/Package.h"
#include "common_agent.h"


using namespace qpid::management;
using namespace qpid::client;
using namespace std;
namespace _qmf = qmf::org::cloudpolicyengine;

// Global Variables
ManagementAgent::Singleton* singleton;
Logger& l = Logger::instance();

void
shutdown(int /*signal*/)
{
	exit(0);
}

struct option opt[] = {
	{"help", no_argument, NULL, 'h'},
	{"daemon", no_argument, NULL, 'd'},
	{"broker", required_argument, NULL, 'b'},
	{"gssapi", no_argument, NULL, 'g'},
	{"username", required_argument, NULL, 'u'},
	{"password", required_argument, NULL, 'P'},
	{"service", required_argument, NULL, 's'},
	{"port", required_argument, NULL, 'p'},
	{0, 0, 0, 0}
};

static void
print_usage(const char *proc_name)
{
	printf("Usage:\tcped <options>\n");
	printf("\t-d | --daemon     run as a daemon.\n");
	printf("\t-h | --help       print this help message.\n");
	printf("\t-b | --broker     specify broker host name..\n");
	printf("\t-g | --gssapi     force GSSAPI authentication.\n");
	printf("\t-u | --username   username to use for authentication purproses.\n");
	printf("\t-P | --password   password to use for authentication purproses.\n");
	printf("\t-s | --service    service name to use for authentication purproses.\n");
	printf("\t-p | --port       specify broker port.\n");
}

static gboolean
qpid_callback(int fd, gpointer user_data)
{
	ManagementAgent *agent = (ManagementAgent *)user_data;
	QPID_LOG(trace, "Qpid message recieved");
	agent->pollCallbacks();
	return TRUE;
}

static void
qpid_disconnect(gpointer user_data)
{
	QPID_LOG(error, "Qpid connection closed");
}

int
CommonAgent::init(int argc, char **argv, const char* proc_name)
{
	int arg;
	int idx = 0;
	bool daemonize = false;
	bool gssapi = false;
	char *servername = strdup("localhost");
	char *username = NULL;
	char *password = NULL;
	char *service = NULL;
	int serverport = 49000;
	int debuglevel = 0;

	qpid::management::ConnectionSettings settings;
	ManagementAgent *agent;

	log_selector.enable(info);
	log_selector.enable(notice);
	log_selector.enable(warning);
	log_selector.enable(error);

	// Get args
	while ((arg = getopt_long(argc, argv, "hdb:gu:P:s:p:v", opt, &idx)) != -1) {
		switch (arg) {
		case 'h':
		case '?':
			print_usage(proc_name);
			exit(0);
			break;
		case 'd':
			daemonize = true;
			break;
		case 'v':
			debuglevel++;
			break;
		case 's':
			if (optarg) {
				service = strdup(optarg);
			} else {
				print_usage(proc_name);
				exit(1);
			}
			break;
		case 'u':
			if (optarg) {
				username = strdup(optarg);
			} else {
				print_usage(proc_name);
				exit(1);
			}
			break;
		case 'P':
			if (optarg) {
				password = strdup(optarg);
			} else {
				print_usage(proc_name);
				exit(1);
			}
			break;
		case 'g':
			gssapi = true;
			break;
		case 'p':
			if (optarg) {
				serverport = atoi(optarg);
			} else {
				print_usage(proc_name);
				exit(1);
			}
			break;
		case 'b':
			if (optarg) {
				servername = strdup(optarg);
			} else {
				print_usage(proc_name);
				exit(1);
			}
			break;
		default:
			fprintf(stderr, "unsupported option '-%c'.  See --help.\n", arg);
			print_usage(proc_name);
			exit(0);
			break;
		}
	}
	if (debuglevel == 1) {
		log_selector.enable(debug);
	}
	if (debuglevel == 2) {
		log_selector.enable(trace);
	}
	l.select(log_selector);

	if (daemonize == true) {
		if (daemon(0, 0) < 0) {
			fprintf(stderr, "Error daemonizing: %s\n", strerror(errno));
			exit(1);
		}
	}

	// Get our management agent
	singleton = new ManagementAgent::Singleton();
	agent = singleton->getInstance();
	_qmf::Package packageInit(agent);

	// Set up the cleanup handler for sigint
	signal(SIGINT, shutdown);

	// Connect to the broker
	settings.host = servername;
	settings.port = serverport;

	if (username != NULL) {
		settings.username = username;
	}
	if (password != NULL) {
		settings.password = password;
	}
	if (service != NULL) {
		settings.service = service;
	}
	if (gssapi == true) {
		settings.mechanism = "GSSAPI";
	}

	syslog(LOG_INFO, "Connecting to Qpid broker at %s on port %d", servername, serverport);
	agent->setName("cloudpolicyengine.org", proc_name);
	string dataFile(".cloudpolicyengine-data-");
	agent->init(settings, 5, true, dataFile + proc_name);

	/* Do any setup required by our agent */
	if (this->setup(agent) < 0) {
		fprintf(stderr, "Failed to set up broker connection to %s on %d for %s\n",
			servername, serverport, proc_name);
		return -1;
	}

	mainloop = g_main_new(FALSE);
	qpid_source = mainloop_add_fd(G_PRIORITY_HIGH,
				      agent->getSignalFd(),
				      qpid_callback,
				      qpid_disconnect,
				      agent);

	return 0;
}

void
CommonAgent::run()
{
	g_main_run(this->mainloop);
}
