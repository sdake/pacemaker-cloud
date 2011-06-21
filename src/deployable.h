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

#include <qmf/ConsoleSession.h>

#include "pcmk_pe.h"
#include "common_agent.h"

class Assembly;
class Resource;

class Deployable {
private:
	CommonAgent *_agent;
	qpid::messaging::Connection *connection;
	qmf::ConsoleSession *session;

	std::string _name;
	std::string _uuid;
	std::string _dc_uuid;
	std::string _crmd_uuid;
	int _file_count;

	std::map<std::string, Assembly*> _assemblies;
	std::map<std::string, Resource*> _resources;
	std::map<std::string, Assembly*> _agents_ass;

	xmlDocPtr _config;
	xmlDocPtr _pe;
	qpid::sys::Mutex xml_lock;

	int _resource_counter;
	bool _status_changed;
	qb_loop_timer_handle _processing_timer;

	void create_assemblies(xmlNode * nodes);
	void create_services(std::string& ass_name, xmlNode * services);

	int32_t assembly_add(std::string& name,
			     std::string& uuid,
			     std::string& ipaddr);
	int32_t assembly_remove(std::string& name,
				std::string& uuid);

public:
	Deployable();
	Deployable(std::string& uuid, CommonAgent *agent);
	~Deployable();

	const std::string& name_get() const { return _name; }
	const std::string& uuid_get() const { return _uuid; }
	const std::string& dc_uuid_get() const { return _dc_uuid; }
	const std::string& crmd_uuid_get() const { return _crmd_uuid; }
	Assembly* assembly_get(std::string& hostname);
	Resource* resource_get(struct pe_operation * op);

	void reload(void);
	void process(void);
	void schedule_processing(void);
	void resource_execute(struct pe_operation *op);
	void transition_completed(int32_t result);

	void service_state_changed(const std::string& ass_name, std::string& service_name,
				  std::string &state, std::string &reason);
	void assembly_state_changed(Assembly *a, std::string state,
				    std::string reason);

	bool process_qmf_events(void);
	void map_agents_ass(string &agent_name, Assembly *a);
};

