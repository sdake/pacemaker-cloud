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
#ifndef ASSEMBLY_H_DEFINED
#define ASSEMBLY_H_DEFINED

#include <string>
#include <libxml/parser.h>

class Deployable;
class VmLauncher;

class Assembly {
protected:

	uint32_t _state;

	Deployable *_dep;
	VmLauncher *_vml;

	std::string _name;
	std::string _uuid;
	int _refcount;

	void deref(void);

public:
	static const uint32_t STATE_INIT = 0;
	static const uint32_t STATE_OFFLINE = 1;
	static const uint32_t STATE_ONLINE = 2;

	Assembly();
	Assembly(Deployable *dep, VmLauncher *vml, std::string& name,
		 std::string& uuid);
	~Assembly();

	virtual void insert_status(xmlNode *status) {};

	virtual void stop(void);
	virtual void start(void);
	virtual void restart(void);
	virtual void status_response(std::string& status) {};

	uint32_t state_get(void) { return _state; };
	const std::string& name_get(void) const { return _name; }
	const std::string& uuid_get(void) const { return _uuid; }
};
#endif /* ASSEMBLY_H_DEFINED */
