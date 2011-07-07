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
#ifndef QMF_MULTIPLEXER_H_DEFINED
#define QMF_MULTIPLEXER_H_DEFINED

#include "qmf_object.h"

class QmfMultiplexer {
private:
	qpid::messaging::Connection *connection;
	qmf::ConsoleSession *session;

	std::string _url;
	std::string _filter;

	std::list<QmfObject*> _objects;
	std::map<std::string, QmfObject*> _agent_to_obj;
public:
	QmfMultiplexer() {};
	~QmfMultiplexer() {};
	void qmf_object_add(QmfObject *qc);
	void url_set(std::string s) { _url = s; };
	void filter_set(std::string s) { _filter = s; };

	bool process_events(void);
	void start(void);
};
#endif /* QMF_MULTIPLEXER_H_DEFINED */
