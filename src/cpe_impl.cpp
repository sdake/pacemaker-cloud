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
#include <string>
#include <iostream>

#include "cpe_impl.h"
#include "init-dbus.h"
#include "mainloop.h"

using namespace std;

CpeImpl::CpeImpl()
{
        dbus_init();
}

CpeImpl::~CpeImpl()
{
	// TODO Auto-generated destructor stub
}

uint32_t
CpeImpl::dep_start(string& dep_uuid)
{
	int32_t rc = init_job_start("pcloud-dped", dep_uuid.c_str());

	if (rc == 0) {
		qb_log(LOG_INFO, "started dped instance=%s", dep_uuid.c_str());
		return 0;
	} else {
		errno = -rc;
		qb_perror(LOG_ERR, "Failed to start dped instance=%s", dep_uuid.c_str());
		return -rc;
	}
}

uint32_t
CpeImpl::dep_reload(string& dep_uuid)
{
	int32_t rc = init_job_reload("pcloud-dped", dep_uuid.c_str());

	if (rc == 0) {
		qb_log(LOG_INFO, "reloaded dped instance=%s", dep_uuid.c_str());
		return 0;
	} else {
		errno = -rc;
		qb_perror(LOG_ERR, "Failed to reload dped instance=%s", dep_uuid.c_str());
		return -rc;
	}
}

uint32_t
CpeImpl::dep_stop(string& dep_uuid)
{
	int32_t rc = init_job_stop("pcloud-dped", dep_uuid.c_str());

	if (rc == 0) {
		qb_log(LOG_INFO, "stopped dped instance=%s", dep_uuid.c_str());
		return 0;
	} else {
		errno = -rc;
		qb_perror(LOG_ERR, "Failed to stop dped instance=%s", dep_uuid.c_str());
		return -rc;
	}
}

uint32_t
CpeImpl::dep_list(std::list<std::string> * list)
{
	return 0; //TODO list deployables
}
