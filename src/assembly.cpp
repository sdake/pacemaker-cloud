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
#include "config.h"

#include <qb/qblog.h>

#include <iostream>
#include <sstream>
#include <map>

#include "mainloop.h"
#include "assembly.h"
#include "deployable.h"

using namespace std;

void
Assembly::deref(void)
{
	_refcount--;
	if (_refcount == 0) {
		delete this;
	}
}

void
Assembly::start(void)
{
	_vml->start(this);
}

void
Assembly::stop(void)
{
	if (_state > STATE_INIT) {
		_state = STATE_INIT;
		_vml->stop(this);
	}
	deref();
}

void
Assembly::restart(void)
{
	_vml->restart(this);
}

void
Assembly::we_are_going_down(void)
{
	_actual_failures++;
	if (_max_failures > 0 && _failure_period > 0) {
		float p;
		qb_util_stopwatch_stop(_escalation_period);

		p = qb_util_stopwatch_sec_elapsed_get(_escalation_period);
		qb_log(LOG_DEBUG, "assembly escalation [fail:%d/%d period:%5.2f/%d]",
		       _actual_failures, _max_failures, p, _failure_period);
		if (_actual_failures >= _max_failures && p <= _failure_period) {
			_dep->escalate_assembly_failure(this);
			_actual_failures = 0;
			qb_util_stopwatch_start(_escalation_period);
		}
		if (p > _failure_period) {
			/* reset */
			_actual_failures = 0;
			qb_util_stopwatch_start(_escalation_period);
		}
	}
}

Assembly::Assembly() :
	_refcount(1), _dep(NULL),
	_name(""), _uuid(""), _state(STATE_OFFLINE)
{
}

Assembly::~Assembly()
{
	qb_log(LOG_DEBUG, "~Assembly(%s)", _name.c_str());
	if (_max_failures > 0 && _failure_period > 0) {
		qb_util_stopwatch_free(_escalation_period);
	}
}

Assembly::Assembly(Deployable *dep, VmLauncher *vml, std::string& name,
		   std::string& uuid, int num_failures, int failure_period) :
	_refcount(1), _dep(dep), _vml(vml), _name(name), _uuid(uuid),
	_state(STATE_OFFLINE), _max_failures(num_failures),
	_failure_period(failure_period), _actual_failures(0)
{
	_refcount++;
	if (_max_failures > 0 && _failure_period > 0) {
		_escalation_period = qb_util_stopwatch_create();
		qb_util_stopwatch_start(_escalation_period);
	}
}
