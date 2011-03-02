/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of cpe.
 *
 * cpe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * cpe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cpe.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _DEPLOYABLE_H_
#define _DEPLOYABLE_H_

#include <qpid/agent/ManagementAgent.h>
#include "org/cloudpolicyengine/Deployable.h"
#include "dpe.h"

class DeployableAgent : public Manageable
{
private:
	_qmf::Deployable* _mgmtObject;

public:

	DeployableAgent(ManagementAgent* agent, DpeAgent* parent,
			std::string& name, std::string& uuid);
	~DeployableAgent() { _mgmtObject->resourceDestroy(); }

	ManagementObject* GetManagementObject(void) const  { return _mgmtObject; }
	status_t ManagementMethod(uint32_t method, Args& arguments, std::string& text);
	std::string getKey() const { return _mgmtObject->getKey(); }
};

#endif /* _DEPLOYABLE_H_ */

