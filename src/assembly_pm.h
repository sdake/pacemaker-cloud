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
#ifndef ASSEMBLY_PM_H_DEFINED
#define ASSEMBLY_PM_H_DEFINED

#include "assembly.h"
#include "vmlauncher.h"

class AssemblyPm : public Assembly {
private:
	qb_loop_timer_handle state_check_th;
public:
	AssemblyPm();
	AssemblyPm(Deployable *dep, VmLauncher *vml, std::string& name,
		 std::string& uuid);
	~AssemblyPm();

	void stop(void);
	void start(void);
	void restart(void);
	uint32_t state_get(void);
	void check_state(void);
	void status_response(std::string& status);
};
#endif /* ASSEMBLY_PM_H_DEFINED */
