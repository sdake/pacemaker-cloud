/*
 * Copyright (C) 2010-2012 Red Hat, Inc.
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
#ifndef MATAHARI_H_DEFINED
#define MATAHARI_H_DEFINED

#include "qmf_multiplexer.h"
#include "pcmk_pe.h"
#include "trans.h"
#include "cape.h"

class Matahari {
private:
	static const uint32_t HEARTBEAT_INIT = 1;
	static const uint32_t HEARTBEAT_NOT_RECEIVED = 2;
	static const uint32_t HEARTBEAT_OK = 3;
	static const uint32_t HEARTBEAT_SEQ_BAD = 4;
	uint32_t _state;
	std::string _name;
	std::string _uuid;
	uint32_t _hb_state;
	uint32_t _last_sequence;
	GTimer* _last_heartbeat;

	QmfObject _mh_serv;
	QmfObject _mh_rsc;
	QmfObject _mh_host;
	QmfMultiplexer *_mux;
	struct assembly* _node_access;

	typedef uint32_t (Matahari::*fsm_state_fn)(void);
	typedef void (Matahari::*fsm_action_fn)(void);

	fsm_state_fn state_table[NODE_NUM_STATES];
	fsm_action_fn state_action_table[NODE_NUM_STATES][NODE_NUM_STATES];

	uint32_t check_state_online(void);
	uint32_t check_state_offline(void);
	void state_offline_to_online(void);
	void state_online_to_offline(void);

public:
	void heartbeat_recv(uint32_t timestamp, uint32_t sequence);
	void check_state(void);
	void resource_action(struct pe_operation *op);
	uint32_t state_get(void) { return _state; };

	Matahari();
	Matahari(struct assembly* na, QmfMultiplexer *m,
		 std::string& name, std::string& uuid);
	~Matahari();
};
#endif /* MATAHARI_H_DEFINED */
