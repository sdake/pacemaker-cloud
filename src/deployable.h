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
#include <string>
#include <map>

class Assembly;

class Deployable {
private:
	std::string _name;
	std::string _uuid;
	std::map<std::string, Assembly*> assemblies;

public:

	Deployable();
	Deployable(std::string& uuid);
	~Deployable();
	std::string get_name() const { return _name; }
	std::string get_uuid() const { return _uuid; }

	void reload(void);

	int32_t assembly_add(std::string& name,
			     std::string& uuid,
			     std::string& ipaddr);
	int32_t assembly_remove(std::string& name,
			     std::string& uuid);
};



