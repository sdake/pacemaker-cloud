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

#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Duration.h>
#include <qmf/AgentSession.h>
#include <qmf/AgentEvent.h>
#include <qmf/Schema.h>
#include <qmf/SchemaProperty.h>
#include <qmf/SchemaMethod.h>
#include <qmf/Data.h>
#include <qmf/DataAddr.h>
#include <qpid/types/Variant.h>
#include <string>
#include <iostream>

#include "common_agent.h"


using namespace std;
using namespace qmf;
namespace _qmf = qmf::org::cloudpolicyengine;

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
	AgentSession *agent = (AgentSession *)user_data;
printf ("qpid message received\n");
	return TRUE;
}

static void
qpid_disconnect(gpointer user_data)
{
	printf("Qpid connection closed");
}

int
CommonAgent::init(int argc, char **argv, const char *proc_name)
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
	string url = "localhost:49000";

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

	if (daemonize == true) {
		if (daemon(0, 0) < 0) {
			fprintf(stderr, "Error daemonizing: %s\n", strerror(errno));
			exit(1);
		}
	}

	agent_connection = qpid::messaging::Connection(url, "{reconnect:True}");
	agent_connection.open();

	agent_session = AgentSession(agent_connection, "{interval:30}");
	agent_session.setVendor("cloudpolicyengine.org");
	agent_session.setProduct(proc_name);

	package.configure(agent_session);
	agent_session.open();

	// Set up the cleanup handler for sigint
	signal(SIGINT, shutdown);

	syslog(LOG_INFO, "Connecting to Qpid broker at %s on port %d", servername, serverport);

	mainloop = g_main_new(FALSE);
/*
	qpid_source = mainloop_add_fd(G_PRIORITY_HIGH,
				      agent_session.getSignalFd(),
				      qpid_callback,
				      qpid_disconnect,
				      agent_session);
*/

	return 0;
}

void
CommonAgent::run()
{
	g_main_run(this->mainloop);
}
