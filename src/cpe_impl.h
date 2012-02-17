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

#ifndef CPE_IMPL_H_
#define CPE_IMPL_H_

#include <qb/qblog.h>
#include <qb/qbloop.h>
#include "mainloop.h"
#include <string>
#include <iostream>

class CpeImpl {
public:
	CpeImpl();
	virtual ~CpeImpl();

	uint32_t dep_start(std::string& uuid, std::string& monitor);
	uint32_t dep_stop(std::string& uuid, std::string& monitor);
	uint32_t dep_reload(std::string& uuid, std::string& monitor);
	uint32_t dep_list(std::list<std::string> * list);
};

#endif /* CPE_IMPL_H_ */
