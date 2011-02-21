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

#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Duration.h>
#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Data.h>
#include <qpid/types/Variant.h>
#include <string>
#include <iostream>

#include "mainloop.h"

using namespace std;
using namespace qmf;
using qpid::types::Variant;
using qpid::messaging::Duration;

static gboolean
monitor_timeout(gpointer data)
{
	ConsoleSession *session = (ConsoleSession *)data;
	ConsoleEvent event;

	if (session->nextEvent(event)) {
		if (event.getType() == CONSOLE_EVENT) {
			const Data& data(event.getData(0));
			cout << " content=" << data.getProperties() << endl;
		}
	}

	return TRUE;
}

int monitor_new_host(std::string& host_url)
{
	string connectionOptions;
	string sessionOptions;
	int rc;

	qpid::messaging::Connection connection(host_url, connectionOptions);
	connection.open();

	ConsoleSession session(connection, sessionOptions);
	session.open();

	rc = g_timeout_add(3,
                      monitor_timeout,
		      &session);

	if (rc > 0) {
		return 0;
	} else {
		return -1;
	}
}

int monitor_del_host(string& host_url)
{
	return 0;
}

