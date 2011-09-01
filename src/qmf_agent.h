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
#ifndef QMF_AGENT_DEFINED
#define QMF_AGENT_DEFINED

#include <string>

#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Data.h>

#include "qmf_object.h"

class QmfAgent {
private:
	qmf::Agent _agent;
	std::list<QmfObject*> _objects;
	std::map<uint32_t, QmfAsyncRequest*> _outstanding_calls;

public:
	QmfAgent(qmf::Agent& agent);
	~QmfAgent();
	void add(QmfObject *o);

	void process_event(qmf::ConsoleEvent &event);
	void call_method_async(QmfAsyncRequest *req,
				 qmf::Data *data);
};

#endif /* QMF_AGENT_DEFINED */


