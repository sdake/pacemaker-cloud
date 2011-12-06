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
#ifndef VMLAUNCHER_H__DEFINED
#define VMLAUNCHER_H__DEFINED

#include <string>
#include <map>

#include <qmf/ConsoleSession.h>
#include "qmf_multiplexer.h"
#include "assembly.h"

class Deployable;

class VmLauncher {
private:
	QmfObject _vm_launcher;
	Deployable * _dep;

public:
	VmLauncher() {};
	~VmLauncher() {};
	VmLauncher(Deployable* dep);

	void start(Assembly *a);
	void stop(Assembly *a);
	void restart(Assembly *a);
	void status(Assembly *a);
};

#endif /* VMLAUNCHER_H__DEFINED */
