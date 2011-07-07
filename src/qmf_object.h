/*
 * Copyright (C) 2011 Red Hat, Inc.
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
#ifndef QMF_OBJECT_DEFINED
#define QMF_OBJECT_DEFINED

#include <string>
#include <glib.h>

#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Data.h>

#include "qmf_job.h"

class QmfObject {
private:
	bool _connected;
	qmf::Data _qmf_data;

	std::string _agent_name;
	std::string _query;
	std::string _prop_name;
	std::string _prop_value;

	std::map<uint32_t, QmfAsyncRequest*> _outstanding_calls;
	std::list<std::string> _dead_agents;
	std::list<QmfAsyncRequest*> _pending_jobs;
public:
	enum rpc_result {
		RPC_OK,
		RPC_TIMEOUT,
		RPC_NOT_CONNECTED,
		RPC_CANCELLED,
		RPC_EXCEPTION,
	};
	typedef void (method_response_fn)(QmfAsyncRequest* ar,
					   qpid::types::Variant::Map out_args,
					   enum rpc_result rc);
	method_response_fn* _method_response_fn;

	typedef void (event_fn)(qmf::ConsoleEvent& e,
				void *user_data);
	event_fn* _event_fn;
	void* _event_user_data;
	
	typedef void (connection_event_fn)(void *user_data);
	connection_event_fn* _connection_event_fn;
	void* _connection_event_user_data;

	QmfObject() : _connected(false), _method_response_fn(NULL),
       	_connection_event_fn(NULL), _event_fn(NULL) {};
	~QmfObject() {};

	bool is_connected(void) { return _connected; };
	bool connect(qmf::Agent &a);
	void disconnect(void);

	void query_set(std::string q) { _query = q; };
	void prop_set(std::string n, std::string v) {
	       	_prop_name = n;
		_prop_value = v;
	};
	std::string& agent_name_get(void) { return _agent_name; };

	qmf::ConsoleEvent method_call(std::string method,
			      qpid::types::Variant::Map in_args);
	void method_call_async(std::string method,
			 qpid::types::Variant::Map in_args,
			 void *user_data,
			 uint32_t timeout);
	void method_response_handler_set(QmfObject::method_response_fn* fn) {
		_method_response_fn = fn;
	};
	void event_handler_set(QmfObject::event_fn* fn, void *user_data) {
		_event_fn = fn;
		_event_user_data = user_data;
	};
	void connection_event_handler_set(QmfObject::connection_event_fn* fn,
					  void *user_data) {
		_connection_event_fn = fn;
		_connection_event_user_data = user_data;
	};
	void method_response(QmfAsyncRequest* ar,
			     qpid::types::Variant::Map out_args,
			     enum rpc_result rc) {
		g_timer_stop(ar->time_execed);
		ar->state = QmfAsyncRequest::JOB_COMPLETED;
		ar->log();
		(_method_response_fn)(ar, out_args, rc);
	};
	bool process_event(qmf::ConsoleEvent &event);

	void run_pending_calls(void);
};

#endif /* QMF_OBJECT_DEFINED */
