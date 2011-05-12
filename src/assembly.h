/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#include <qpid/messaging/Connection.h>
#include <qpid/messaging/Duration.h>
#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Data.h>
#include <qpid/types/Variant.h>
#include <libxml/parser.h>
#include <string>

class Deployable;

class Assembly {
private:
	std::string connectionOptions;
	std::string sessionOptions;
	qmf::ConsoleSession *session;
	qmf::Data _mh_serv_class;
	bool _mh_serv_class_found;
	qmf::Data _mh_rsc_class;
	bool _mh_rsc_class_found;
	qpid::messaging::Connection *connection;
	uint32_t state;
	uint32_t _last_sequence;
	GTimer* _last_heartbeat;
	Deployable *_dep;

	std::string _name;
	std::string _uuid;
	std::string _ipaddr;
	int refcount;


public:
	static const uint32_t HEARTBEAT_INIT = 1;
	static const uint32_t HEARTBEAT_NOT_RECEIVED = 2;
	static const uint32_t HEARTBEAT_OK = 3;
	static const uint32_t HEARTBEAT_SEQ_BAD = 4;

	bool is_connected;

	Assembly();
	Assembly(Deployable *dep, std::string& name,
		 std::string& uuid, std::string& ipaddr);
	~Assembly();

	bool nextEvent(qmf::ConsoleEvent&);
	void stop(void);
	uint32_t state_get(void) { return this->state; };
	void deref(void);
	void check_heartbeat(void);
	void check_heartbeat(uint32_t timestamp, uint32_t sequence);

	void insert_status(xmlNode *status);

	void matahari_discover(void);

	void resource_failed(void);
	uint32_t _resource_execute(struct pe_operation *op);
	uint32_t resource_execute(struct pe_operation *op);
};


