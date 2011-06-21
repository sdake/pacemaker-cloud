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
#include <libxml/parser.h>

struct operation_history {
	std::string* rsc_id;
	std::string* operation;
	uint32_t call_id;
	uint32_t interval;
	enum ocf_exitcode rc;
	uint32_t target_outcome;
	time_t last_run;
	time_t last_rc_change;
	uint32_t graph_id;
	uint32_t action_id;
	void *resource;
	char *op_digest;
};

class Deployable;

class Resource {
private:
	std::string _id;
	std::string _type;
	std::string _class;
	std::string _provider;
	Deployable *_dep;

	void save(struct pe_operation *op, enum ocf_exitcode ec);

public:
	qb_loop_timer_handle _monitor_timer;
	struct pe_operation *_monitor_op;
	Resource();
	Resource(Deployable *d, std::string& id, std::string& type,
		 std::string& class_name, std::string& provider);
	~Resource();

	void stop(struct pe_operation *op);
	void delete_op_history(struct pe_operation *op);
	void start_recurring(struct pe_operation *op);
	void execute(struct pe_operation *op);
	void __execute(struct pe_operation *op);
	void completed(struct pe_operation *op, enum ocf_exitcode ec);
	void failed(struct pe_operation *op);
	xmlNode * insert_status(xmlNode *rscs);
};
