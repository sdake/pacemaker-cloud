/* mh_agent.cpp - Copyright (C) 2010 Red Hat, Inc.
 * Written by Andrew Beekhof <andrew@beekhof.net>
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
#include "agent.h"


using namespace qpid::management;
using namespace qpid::client;
using namespace std;
namespace _qmf = qmf::org::cloudpolicyengine;

// Global Variables
ManagementAgent::Singleton* singleton;

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
mh_qpid_callback(int fd, gpointer user_data)
{
	ManagementAgent *agent = (ManagementAgent *)user_data;
	//mh_trace("Qpid message recieved");
	agent->pollCallbacks();
	return TRUE;
}

static void
mh_qpid_disconnect(gpointer user_data)
{
	//mh_err("Qpid connection closed");
}

int
CpeAgent::init(int argc, char **argv, const char* proc_name)
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

	/* Set up basic logging */
	openlog(NULL, LOG_PERROR|LOG_PID, LOG_DAEMON);
	//    mh_log_init(proc_name, LOG_INFO, FALSE);

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
			//mh_log_level++;
			//mh_enable_stderr(1);
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

	if (daemonize == true) {
		if (daemon(0, 0) < 0) {
			fprintf(stderr, "Error daemonizing: %s\n", strerror(errno));
			exit(1);
		}
	}

	/* Re-initialize logging now that we've completed option processing */
	//mh_log_init(proc_name, LOG_INFO+debuglevel, debuglevel > 0);

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
	std::string dataFile(".cloudpolicyengine-data-");
	agent->init(settings, 5, true, dataFile + proc_name);

	/* Do any setup required by our agent */
	if (this->setup(agent) < 0) {
		fprintf(stderr, "Failed to set up broker connection to %s on %d for %s\n",
				servername, serverport, proc_name);
		return -1;
	}

	this->mainloop = g_main_new(FALSE);
	this->qpid_source = mainloop_add_fd(G_PRIORITY_HIGH,
										agent->getSignalFd(),
										mh_qpid_callback,
										mh_qpid_disconnect,
										agent);

	return 0;
}

void
CpeAgent::run()
{
	g_main_run(this->mainloop);
}
