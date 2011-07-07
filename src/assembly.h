/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
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
#ifndef ASSEMBLY_H_DEFINED
#define ASSEMBLY_H_DEFINED

#include "qmf_multiplexer.h"

class Deployable;

class Assembly {
private:
	static const uint32_t HEARTBEAT_INIT = 1;
	static const uint32_t HEARTBEAT_NOT_RECEIVED = 2;
	static const uint32_t HEARTBEAT_OK = 3;
	static const uint32_t HEARTBEAT_SEQ_BAD = 4;

	typedef uint32_t (Assembly::*fsm_state_fn)(void);
	typedef void (Assembly::*fsm_action_fn)(void);

	QmfObject _mh_serv;
	//QmfObject _mh_rsc;
	QmfObject _mh_host;

	uint32_t _hb_state;
	uint32_t _state;
	static const uint32_t NUM_STATES = 3;
	fsm_state_fn state_table[NUM_STATES];
	fsm_action_fn state_action_table[NUM_STATES][NUM_STATES];
	uint32_t _last_sequence;
	GTimer* _last_heartbeat;
	Deployable *_dep;

	std::string _name;
	std::string _uuid;
	int _refcount;

	std::map<uint32_t, struct pe_operation*> _ops;

	uint32_t check_state_online(void);
	uint32_t check_state_offline(void);

	void state_offline_to_online(void);
	void state_online_to_offline(void);

	void deref(void);
	void insert_op_history(xmlNode *rsc, struct operation_history *oh);

public:
	void heartbeat_recv(uint32_t timestamp, uint32_t sequence);
	void check_state(void);

	static const uint32_t STATE_INIT = 0;
	static const uint32_t STATE_OFFLINE = 1;
	static const uint32_t STATE_ONLINE = 2;
	std::map<std::string, struct operation_history*> op_history;

	Assembly();
	Assembly(Deployable *dep, std::string& name,
		 std::string& uuid);
	~Assembly();

	void matahari_discover(qmf::ConsoleSession *session);
	void stop(void);
	uint32_t state_get(void) { return _state; };
	const std::string& name_get(void) const { return _name; }
	const std::string& uuid_get(void) const { return _uuid; }

	void insert_status(xmlNode *status);

	void resource_execute(struct pe_operation *op, std::string method, qpid::types::Variant::Map args);
	struct pe_operation * op_remove_by_correlator(uint32_t correlator);

	bool process_qmf_events(qmf::ConsoleEvent &event);

};
#endif /* ASSEMBLY_H_DEFINED */
