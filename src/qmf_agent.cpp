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
#include <qb/qblog.h>
#include "qmf_agent.h"

using namespace std;
using namespace qmf;


QmfAgent::QmfAgent(Agent& a)
{
	_agent = a;
}

void QmfAgent::add(QmfObject *o)
{
	_objects.push_back(o);
	o->agent_set(this);
}

QmfAgent::~QmfAgent()
{
	QmfAsyncRequest *ar;
	qpid::types::Variant::Map empty_args;

	map<uint32_t, QmfAsyncRequest*>::iterator call;
	for (call = _outstanding_calls.begin();
	     call != _outstanding_calls.end(); call++) {
		ar = call->second;
		ar->obj->method_response(ar, empty_args, QmfObject::RPC_CANCELLED);
	}

	for (list<QmfObject*>::iterator obj = _objects.begin();
	     obj != _objects.end(); ++obj) {
		(*obj)->disconnect();
	}
}


void QmfAgent::call_method_async(QmfAsyncRequest *ar,
				 const DataAddr &addr)
{
	uint32_t correlation_id = _agent.callMethodAsync(ar->method, ar->args, addr);
	_outstanding_calls[correlation_id] = ar;
}

void
QmfAgent::process_event(qmf::ConsoleEvent &event)
{
	list<QmfObject*>::iterator iter;
	QmfAsyncRequest *ar;

	if (event.getType() == CONSOLE_METHOD_RESPONSE ||
	    event.getType() == CONSOLE_EXCEPTION) {
		ar = _outstanding_calls[event.getCorrelator()];
		if (ar) {
			ar->obj->process_event(event, ar);
			_outstanding_calls.erase(event.getCorrelator());
		} else {
			qb_log(LOG_WARNING, "unknown request");
		}
		return;
	}
	for (list<QmfObject*>::iterator it = _objects.begin();
	     it != _objects.end(); ++it) {
		(*it)->process_event(event, NULL);
	}
}
