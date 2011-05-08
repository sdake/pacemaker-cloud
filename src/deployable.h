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
#include <string>
#include <map>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <qpid/sys/Mutex.h>

class Assembly;

class Deployable {
private:
	std::string _name;
	std::string _uuid;
	std::map<std::string, Assembly*> _assemblies;
	xmlDocPtr _config;
	xmlDocPtr _pe;
	qpid::sys::Mutex xml_lock;
	int _resource_counter;
	bool _status_changed;

	void services2resources(xmlNode * pcmk_config, xmlNode * services);
	void assemblies2nodes(xmlNode * pcmk_config, xmlNode * nodes);

	int32_t assembly_add(std::string& name,
			     std::string& uuid,
			     std::string& ipaddr);
	int32_t assembly_remove(std::string& name,
				std::string& uuid);

public:

	Deployable();
	Deployable(std::string& uuid);
	~Deployable();
	const std::string& get_name() const { return _name; }
	const std::string& get_uuid() const { return _uuid; }

	void reload(void);
	void process(void);
	void status_changed(void);
	Assembly* assembly_get(std::string& hostname);
};



