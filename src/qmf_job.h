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
#ifndef QMF_JOB_H_DEFINED
#define QMF_JOB_H_DEFINED

#include <string>
#include <glib.h>

#include <qmf/ConsoleSession.h>
#include <qmf/ConsoleEvent.h>
#include <qmf/Data.h>

class QmfObject;

class QmfAsyncRequest {
private:
	uint32_t ref_count;
public:
	enum job_state {
		JOB_INIT,
		JOB_SCHEDULED,
		JOB_RUNNING,
		JOB_COMPLETED,
	};
	enum job_state state;
	QmfObject *obj;
	qpid::types::Variant::Map args;
	std::string method;
	void *user_data;
	uint32_t timeout;
	GTimer* time_queued;
	GTimer* time_execed;

	void ref() { ref_count++; };
	void unref() {
		ref_count--;
		if (ref_count == 0) {
			delete this;
		}
	};

	QmfAsyncRequest() : ref_count(1), state(JOB_INIT) {
		time_execed = g_timer_new();
		time_queued = g_timer_new();
	};
	~QmfAsyncRequest() {
		g_timer_destroy(time_execed);
		g_timer_destroy(time_queued);
	};
	void log(int rc);
};

#endif /* QMF_JOB_H_DEFINED */
