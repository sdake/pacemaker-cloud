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
#ifndef ASSEMBLY_AM_H_DEFINED
#define ASSEMBLY_AM_H_DEFINED

#include "assembly.h"
#include "qmf_multiplexer.h"

class Resource;

class AssemblyAm : public Assembly {
private:
	static const uint32_t HEARTBEAT_INIT = 1;
	static const uint32_t HEARTBEAT_NOT_RECEIVED = 2;
	static const uint32_t HEARTBEAT_OK = 3;
	static const uint32_t HEARTBEAT_SEQ_BAD = 4;
	uint32_t _hb_state;
	uint32_t _last_sequence;
	GTimer* _last_heartbeat;

	QmfObject _mh_serv;
	QmfObject _mh_rsc;
	QmfObject _mh_host;

	typedef uint32_t (AssemblyAm::*fsm_state_fn)(void);
	typedef void (AssemblyAm::*fsm_action_fn)(void);

	static const uint32_t NUM_STATES = 3;
	fsm_state_fn state_table[NUM_STATES];
	fsm_action_fn state_action_table[NUM_STATES][NUM_STATES];
	
	std::map<std::string, struct operation_history*> op_history;

	uint32_t check_state_online(void);
	uint32_t check_state_offline(void);
	void state_offline_to_online(void);
	void state_online_to_offline(void);

public:
	bool process_qmf_events(qmf::ConsoleEvent &event);
	void heartbeat_recv(uint32_t timestamp, uint32_t sequence);
	void check_state(void);

	AssemblyAm();
	AssemblyAm(Deployable *dep, VmLauncher *vml, std::string& name,
		 std::string& uuid);
	~AssemblyAm();

	void service_execute(struct pe_operation *op, std::string method,
			     qpid::types::Variant::Map in_args);
	void resource_execute(struct pe_operation *op, std::string method,
			      qpid::types::Variant::Map args);

	void op_history_del_by_resource(Resource* r);
	void op_history_save(Resource* r, struct pe_operation *op,
			     enum ocf_exitcode ec);
	void op_history_clear(void);
	void op_history_insert(xmlNode *rsc, struct operation_history *oh);
	void insert_status(xmlNode *status);
};
#endif /* ASSEMBLY_AM_H_DEFINED */
