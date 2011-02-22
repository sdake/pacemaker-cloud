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

class Assembly {
  private:
	std::string connectionOptions;
	std::string sessionOptions;
	qmf::ConsoleSession *session;
	qpid::messaging::Connection *connection;
  public:
	std::string name;
	bool is_connected;
	int refcount;

	Assembly();
	Assembly(std::string& host_url);
	~Assembly();

	bool nextEvent(qmf::ConsoleEvent&);
	void stop(void);
	void deref(void);
};


int assembly_monitor_start(std::string& host_url);
int assembly_monitor_stop(std::string& host_url);



