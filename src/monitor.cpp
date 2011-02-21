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

using namespace std;
using namespace qmf;
using qpid::types::Variant;
using qpid::messaging::Duration;

int main(int argc, char** argv)
{
	string connectionOptions;
	string sessionOptions;
	string url;

	if (argc > 1) {
		url = argv[1];
	} else {
		url = "localhost";
	}
	if (argc > 2)
		connectionOptions = argv[2];
	if (argc > 3)
		sessionOptions = argv[3];

	qpid::messaging::Connection connection(url, connectionOptions);
	connection.open();

	ConsoleSession session(connection, sessionOptions);
	session.open();

	while (true) {
		ConsoleEvent event;
		if (session.nextEvent(event)) {
			if (event.getType() == CONSOLE_EVENT) {
				const Data& data(event.getData(0));
				//cout << "Event: timestamp=" << event.getTimestamp() << " severity=" <<
				//    event.getSeverity() << " content=" << data.getProperties() << endl;
				cout << " content=" << data.getProperties() << endl;
			}
		}
	}
}

