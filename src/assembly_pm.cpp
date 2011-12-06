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

#include "mainloop.h"
#include "assembly_pm.h"
#include "deployable.h"

using namespace std;

static void
check_state_tmo(void* data)
{
	AssemblyPm* a = (AssemblyPm*)data;
	a->check_state();
}

AssemblyPm::AssemblyPm() :
	Assembly()
{
	qb_enter();
}

AssemblyPm::AssemblyPm(Deployable *dep, VmLauncher *vml, std::string& name,
		       std::string& uuid) :
	Assembly(dep, vml, name, uuid)
{
	qb_enter();
}

AssemblyPm::~AssemblyPm()
{
	qb_enter();
}

void
AssemblyPm::stop(void)
{
	qb_enter();
	_vml->stop(this);
	mainloop_timer_del(state_check_th);
}

void
AssemblyPm::start(void)
{
	qb_enter();
	_vml->start(this);
	mainloop_timer_add(5000, this,
			   check_state_tmo, &state_check_th);
}

void
AssemblyPm::restart(void)
{
	qb_enter();
	mainloop_timer_del(state_check_th);
	_vml->restart(this);
	mainloop_timer_add(5000, this,
			   check_state_tmo, &state_check_th);
}

uint32_t
AssemblyPm::state_get(void)
{
	return _state;
}

void
AssemblyPm::check_state(void)
{
	_vml->status(this);
	mainloop_timer_add(5000, this,
			   check_state_tmo, &state_check_th);
}

void
AssemblyPm::status_response(std::string& status)
{
	uint32_t new_state;
	if (status == "start" ||
	    status == "pending") {
		new_state = STATE_INIT;
	} else if (status == "running") {
		new_state = STATE_ONLINE;
	} else {
		new_state = STATE_OFFLINE;
	}
	if (new_state != _state) {
		_state = new_state;
		if (new_state == STATE_ONLINE) {
			qb_log(LOG_NOTICE, "AssemblyPm (%s) STATE_ONLINE.",
			       _name.c_str());
			_dep->assembly_state_changed(this, "running", "All good");
		} else {
			mainloop_timer_del(state_check_th);
			qb_log(LOG_NOTICE, "AssemblyPm (%s) STATE_OFFLINE.",
			       _name.c_str());
			_dep->assembly_state_changed(this, "failed", "Not reachable");
		}
	}
}

