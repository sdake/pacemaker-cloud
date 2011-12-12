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
#include <uuid/uuid.h>
#include <libxml/parser.h>
#include <libxslt/transform.h>

#include <string>
#include <algorithm>
#include <map>

#include "pcmk_pe.h"
#include "mainloop.h"
#include "config_loader.h"
#include "deployable.h"
#include "assembly_am.h"
#include "assembly_pm.h"
#include "resource.h"

using namespace std;

Deployable::Deployable(std::string& uuid, CommonAgent *agent) :
	_name(""), _uuid(uuid), _crmd_uuid(""), _config(NULL), _pe(NULL),
	_status_changed(false), _agent(agent), _file_count(0),
	_resource_counter(0), _escalation_pending(false)
{
	uuid_t tmp_id;
	char tmp_id_s[37];

	xmlInitParser();

	uuid_generate(tmp_id);
	uuid_unparse(tmp_id, tmp_id_s);
	_crmd_uuid.insert(0, (char*)tmp_id_s, sizeof(tmp_id_s));

	url_set("localhost:49000");
	filter_set("[or, [eq, _vendor, [quote, 'matahariproject.org']], [eq, _vendor, [quote, 'pacemakercloud.org']]]");
	_vml = new VmLauncher(this);
	start();
	reload();
}

Deployable::~Deployable()
{
	/* Shutdown libxml */
	xmlCleanupParser();
}

void
Deployable::create_services(string& ass_name, xmlNode * services)
{
	xmlNode *cur_node = NULL;
	string name;
	string nm;
	string type;
	string cl;
	string pr;
	string escalation_failures;
	long val;
	int num_failures = -1;
	string escalation_period;
	int failure_period = -1;
	char *endptr;

	for (cur_node = services; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		nm = (char*)xmlGetProp(cur_node, BAD_CAST "name");
		type = (char*)xmlGetProp(cur_node, BAD_CAST "type");
		name = "rsc_";
		name += ass_name;
		name += "_";
		name += nm;

		cl = (char*)xmlGetProp(cur_node, BAD_CAST "class");
		pr = (char*)xmlGetProp(cur_node, BAD_CAST "provider");
		escalation_failures = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_failures");

		val = strtol(escalation_failures.c_str(), &endptr, 10);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) ||
		    (endptr == escalation_failures.c_str())) {
			num_failures = -1;
		} else {
			num_failures = val;
		}

		escalation_period = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_period");
		val = strtol(escalation_period.c_str(), &endptr, 10);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) ||
		    (endptr == escalation_period.c_str())) {
			failure_period = -1;
		} else {
			failure_period = val;
		}

		qb_log(LOG_DEBUG, "loading service: %s", name.c_str());

		if (_resources[name] == NULL) {
			_resources[name] = new Resource(this, name,
							type, cl, pr,
							num_failures,
							failure_period);
		}
	}
}

void
Deployable::stop(void)
{
	for (map<string, Assembly*>::iterator a_iter = _assemblies.begin();
	     a_iter != _assemblies.end(); a_iter++) {
		a_iter->second->stop();
	}
}

void
Deployable::create_assemblies(xmlNode * assemblies)
{
	string ass_name;
	string ass_uuid;
	string start = "start";
	xmlNode *cur_node = NULL;
	xmlNode *child_node = NULL;
	string escalation_failures;
	long val;
	int num_failures = -1;
	string escalation_period;
	int failure_period = -1;
	char *endptr;

	for (cur_node = assemblies; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		ass_name = (char*)xmlGetProp(cur_node, BAD_CAST "name");
		ass_uuid = (char*)xmlGetProp(cur_node, BAD_CAST "uuid");
		escalation_failures = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_failures");

		val = strtol(escalation_failures.c_str(), &endptr, 10);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) ||
		    (endptr == escalation_failures.c_str())) {
			num_failures = -1;
		} else {
			num_failures = val;
		}

		escalation_period = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_period");
		val = strtol(escalation_period.c_str(), &endptr, 10);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) ||
		    (endptr == escalation_period.c_str())) {
			failure_period = -1;
		} else {
			failure_period = val;
		}

		/* make sure the uuid is uppercase to match what dmidecode
		 * produces.
		 */
		std::transform(ass_uuid.begin(), ass_uuid.end(), ass_uuid.begin(), ::toupper);

		qb_log(LOG_DEBUG, "loading assembly: %s", ass_name.c_str());
		if (_active_monitoring) {
			for (child_node = cur_node->children; child_node; child_node = child_node->next) {
				if (child_node->type != XML_ELEMENT_NODE) {
					continue;
				}
				if (strcmp((char*)child_node->name, "services") == 0) {
					create_services(ass_name, child_node->children);
				}
			}
		}
		assembly_add(ass_name, ass_uuid, num_failures, failure_period);
	}
}

void
Deployable::reload(void)
{
	int32_t rc;
	xmlNode *cur_node = NULL;
	xmlNode *dep_node = NULL;
	xsltStylesheetPtr ss = NULL;
	const char *params[1];
	::qpid::sys::Mutex::ScopedLock _lock(xml_lock);

	qb_log(LOG_DEBUG, "reloading config for %s", _uuid.c_str());
	if (_config != NULL) {
		xmlFreeDoc(_config);
		_config = NULL;
	}
	if (_pe != NULL) {
		xmlFreeDoc(_pe);
		_pe = NULL;
	}

	rc = config_get(_uuid, &_config);
	if (rc != 0) {
		qb_log(LOG_ERR, "unable to load XML config file");
		// O, crap
		// try again later?
		_config = NULL;
		return;
	}
	_resource_counter = 1;

	ss = xsltParseStylesheetFile(BAD_CAST "/usr/share/pacemaker-cloud/cf2pe.xsl");
	params[0] = NULL;
	_pe = xsltApplyStylesheet(ss, _config, params);

	dep_node = xmlDocGetRootElement(_config);
	string monitor = (char*)xmlGetProp(dep_node, BAD_CAST "monitor");
	_active_monitoring = (monitor != "passive");
	_username = (char*)xmlGetProp(dep_node, BAD_CAST "username");

	for (cur_node = dep_node->children; cur_node;
	     cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			if (strcmp((char*)cur_node->name, "assemblies") == 0) {
				create_assemblies(cur_node->children);
			}
		}
	}

	xsltFreeStylesheet(ss);

	for (map<string, Assembly*>::iterator a_iter = _assemblies.begin();
	     a_iter != _assemblies.end(); a_iter++) {
		Assembly *a = a_iter->second;
		if (a->state_get() == Assembly::STATE_ONLINE) {
			schedule_processing();
			break;
		}
	}
}

static void
_status_timeout(void *data)
{
	Deployable *d = (Deployable *)data;

	if (pe_is_busy_processing()) {
		// try later
		qb_log(LOG_DEBUG, "pe_is_busy_processing: trying later");
		d->schedule_processing();
	} else {
		d->process();
	}
}

static void
resource_execute_cb(struct pe_operation *op)
{
	Deployable *d = (Deployable *)op->user_data;
	d->resource_execute(op);
}

void
Deployable::resource_execute(struct pe_operation *op)
{
	Resource *r = resource_get(op);
	assert(r != NULL);

	if (op->interval > 0 && strcmp(op->method, "monitor") == 0) {
		r->start_recurring(op);
	} else if (strcmp(op->method, "delete") == 0) {
		r->delete_op_history(op);
	} else if (strcmp(op->method, "stop") == 0) {
		r->stop(op);
	} else {
		r->execute(op);
	}
}

static void
transition_completed_cb(void* user_data, int32_t result)
{
	Deployable *d = (Deployable *)user_data;
	d->transition_completed(result);
}

void
Deployable::transition_completed(int32_t result)
{
}

Resource*
Deployable::resource_get(struct pe_operation *op)
{
	string name = op->rname;
	return _resources[name];
}

Assembly*
Deployable::assembly_get(std::string& node_uuid)
{
	std::transform(node_uuid.begin(), node_uuid.end(), node_uuid.begin(), ::toupper);
	return _assemblies[node_uuid];
}

void
Deployable::process(void)
{
	xmlNode * cur_node = NULL;
	xmlNode * pe_root = NULL;
	xmlNode * status = NULL;
	xmlNode * rscs = NULL;
	int32_t rc = 0;
	Assembly *a;
	Resource *r;
	::qpid::sys::Mutex::ScopedLock _lock(xml_lock);

	if (_pe == NULL || !_active_monitoring) {
		return;
	}
	_status_changed = true;

	if (_dc_uuid.length() == 0 ||
	    _assemblies[_dc_uuid]->state_get() != Assembly::STATE_ONLINE) {
		for (map<string, Assembly*>::iterator a_iter = _assemblies.begin();
		     a_iter != _assemblies.end(); a_iter++) {
			a = a_iter->second;
			if (a->state_get() == Assembly::STATE_ONLINE) {
				_dc_uuid = a->uuid_get();
			}
		}
	}

	pe_root = xmlDocGetRootElement(_pe);
	xmlSetProp(pe_root, BAD_CAST "dc-uuid", BAD_CAST dc_uuid_get().c_str());
	for (cur_node = pe_root->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE &&
		    strcmp((char*)cur_node->name, "status") == 0) {
			status = cur_node;
			break;
		}
	}
	if (status) {
		xmlUnlinkNode(status);
		xmlFreeNode(status);
	}
	status = xmlNewChild(pe_root, NULL, BAD_CAST "status", NULL);

	for (map<string, Assembly*>::iterator a_iter = _assemblies.begin();
	     a_iter != _assemblies.end(); a_iter++) {
		a = a_iter->second;
		a->insert_status(status);
	}

#if 0
	stringstream nf;
	nf << "/var/lib/pacemaker-cloud/pe-input-" << _file_count <<  ".xml";
	_file_count++;

	qb_log(LOG_INFO, "processing new state with %s", nf.str().c_str());
	xmlSaveFormatFileEnc(nf.str().c_str(), _pe, "UTF-8", 1);
#endif
	rc = pe_process_state(pe_root, resource_execute_cb,
			      transition_completed_cb, this);
	_status_changed = false;
	if (rc != 0) {
		schedule_processing();
	}
}

void
Deployable::schedule_processing(void)
{
	if (_status_changed) {
		qb_log(LOG_DEBUG, "not scheduling - collecting status");
		return;
	}

	if (mainloop_timer_is_running(_processing_timer)) {
		qb_log(LOG_DEBUG, "not scheduling - already scheduled");
	} else {
		mainloop_timer_add(1000, this,
				   _status_timeout,
				   &_processing_timer);
	}
}

void
Deployable::escalate_service_failure(AssemblyAm *a,
				     const std::string& service_name)
{
	qb_loop_timer_handle th;
	qmf::Data event = qmf::Data(_agent->package.event_assembly_state_change);

	qb_log(LOG_NOTICE, "Escalating failure of service %s to assembly %s:%s",
	       service_name.c_str(), _uuid.c_str(), a->name_get().c_str());
	event.setProperty("deployable", _uuid);
	event.setProperty("assembly", a->name_get());
	event.setProperty("state", "failed");
	event.setProperty("reason", "escalating service failure");
	_agent->agent_session.raiseEvent(event);

	a->escalate();
}

void
Deployable::escalate_assembly_failure(Assembly *a)
{
	qb_loop_timer_handle th;
	qmf::Data event = qmf::Data(_agent->package.event_assembly_state_change);

	qb_log(LOG_NOTICE, "Escalating failure of assembly %s to deployable %s",
	       a->name_get().c_str(), _uuid.c_str());
	event.setProperty("deployable", _uuid);
	event.setProperty("assembly", a->name_get());
	event.setProperty("state", "failed");
	event.setProperty("reason", "escalating assembly failure");
	_agent->agent_session.raiseEvent(event);

	_escalation_pending = true;
	stop();
	qb_loop_stop(_agent->mainloop);
}

void
Deployable::service_state_changed(const string& ass_name, string& service_name,
				  string &state, string &reason)
{
	qmf::Data event = qmf::Data(_agent->package.event_service_state_change);

	event.setProperty("deployable", _uuid);
	event.setProperty("assembly", ass_name);
	event.setProperty("service", service_name);
	event.setProperty("state", state);
	event.setProperty("reason", reason);
	_agent->agent_session.raiseEvent(event);
}

void
Deployable::assembly_state_changed(Assembly *a, string state, string reason)
{
	qb_loop_timer_handle th;
	qmf::Data event = qmf::Data(_agent->package.event_assembly_state_change);

	event.setProperty("deployable", _uuid);
	event.setProperty("assembly", a->name_get());
	event.setProperty("state", state);
	event.setProperty("reason", reason);
	_agent->agent_session.raiseEvent(event);
	if (_escalation_pending) {
		exit(EXIT_FAILURE);
	}
	schedule_processing();
	if (state == "failed") {
		a->restart();
	}
}

int32_t
Deployable::assembly_add(string& name, string& uuid,
			 int num_failures, int failure_period)
{
	Assembly *a = _assemblies[uuid];
	if (a) {
		// don't want duplicates
		return -1;
	}

	try {
		if (_active_monitoring) {
			a = new AssemblyAm(this, _vml, name, uuid, num_failures, failure_period);
		} else {
			a = new AssemblyPm(this, _vml, name, uuid, num_failures, failure_period);
		}
		a->start();
	} catch (qpid::types::Exception e) {
		qb_log(LOG_ERR, "Exception creating Assembly %s",
		       e.what());
		delete a;
		return -1;
	}
	_assemblies[uuid] = a;
	return 0;
}

int32_t
Deployable::assembly_remove(string& uuid, string& name)
{
	Assembly *a = _assemblies[uuid];
	if (a) {
		_assemblies.erase(uuid);
		a->stop();
	}
	return 0;
}

