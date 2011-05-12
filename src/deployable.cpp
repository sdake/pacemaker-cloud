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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <glib.h>
#include <libxml/parser.h>
#include "pcmk_pe.h"
#include <qb/qblog.h>

#include <string>
#include <map>

#include "config_loader.h"
#include "deployable.h"
#include "assembly.h"

using namespace std;

Deployable::Deployable(std::string& uuid)
{
	_name = "";
	_uuid = uuid;
	_config = NULL;
	_pe = NULL;
	_status_changed = false;
	xmlInitParser();
	reload();
}

Deployable::~Deployable()
{
	map<string, Assembly*>::iterator kill;
	map<string, Assembly*>::iterator iter;
	Assembly *a;

	for (iter = _assemblies.begin(); iter != _assemblies.end(); ) {
		kill = iter;
		a = kill->second;
		iter++;
		_assemblies.erase(kill);
		a->stop();
	}
	/* Shutdown libxml */
	xmlCleanupParser();
}


/*
   <nodes>
   <node id="node1" uname="node1" type="member"/>
   <node id="node2" uname="node2" type="member"/>
   </nodes>
   */
void
Deployable::assemblies2nodes(xmlNode * pcmk_config, xmlNode * assemblies)
{
	string ass_name;
	string ass_uuid;
	string ass_ip;
	xmlNode *cur_node = NULL;
	xmlNode *nodes = xmlNewChild(pcmk_config, NULL, BAD_CAST "nodes", NULL);
	xmlNode *node = NULL;

	for (cur_node = assemblies; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			qb_log(LOG_DEBUG, "node name: %s", cur_node->name);
			ass_name = (char*)xmlGetProp(cur_node, BAD_CAST "name");
			ass_uuid = ass_name/* FIXME (char*)xmlGetProp(cur_node, BAD_CAST "uuid")*/;
			ass_ip = (char*)xmlGetProp(cur_node, BAD_CAST "ipaddr");
			assert(ass_ip.length() > 0);

			node = xmlNewChild(nodes, NULL, BAD_CAST "node", NULL);
			xmlNewProp(node, BAD_CAST "id", BAD_CAST ass_name.c_str());
			xmlNewProp(node, BAD_CAST "uname", BAD_CAST ass_name.c_str());
			xmlNewProp(node, BAD_CAST "type", BAD_CAST "member");

			assembly_add(ass_name, ass_uuid, ass_ip);
		}
	}
}

/*
   <resources>
   <primitive id="rsc1" class="heartbeat" type="apache"/>
   <primitive id="rsc2" class="heartbeat" type="apache"/>
   </resources>
   */
void
Deployable::services2resources(xmlNode * pcmk_config, xmlNode * services)
{
	xmlNode *cur_node = NULL;
	xmlNode *resource = NULL;
	xmlChar *name = NULL;
	xmlChar *ha = NULL;
	xmlChar res_id[128];
	xmlNode *resources;
	xmlNode *operations = NULL;
	xmlNode *op = NULL;
	xmlChar op_id[128];

	resources = xmlNewChild(pcmk_config, NULL, BAD_CAST "resources", NULL);

	for (cur_node = services; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			name = xmlGetProp(cur_node, BAD_CAST "name");
			ha = xmlGetProp(cur_node, BAD_CAST "HA");
			snprintf((char*)res_id, 128, "res-%d", _resource_counter);
			_resource_counter++;

			resource = xmlNewChild(resources, NULL, BAD_CAST "primitive", NULL);
			xmlNewProp(resource, BAD_CAST "id", res_id);
			xmlNewProp(resource, BAD_CAST "class", BAD_CAST "lsb");
			xmlNewProp(resource, BAD_CAST "type", name);
			if (ha && strcmp((char*)ha, "True") == 0) {
				operations = xmlNewChild(resource, NULL, BAD_CAST "operations", NULL);
				op = xmlNewChild(operations, NULL, BAD_CAST "op", NULL);
				snprintf((char*)op_id, 128, "monitor-%s", (char*)res_id);

				xmlNewProp(op, BAD_CAST "id", op_id);
				xmlNewProp(op, BAD_CAST "name", BAD_CAST "monitor");
				xmlNewProp(op, BAD_CAST "interval", BAD_CAST "10s");
			}
		}
	}
}

void
Deployable::reload(void)
{
	int32_t rc;
	xmlNode *cur_node = NULL;
	xmlNode *dep_node = NULL;
	xmlNode *cib = NULL;
	xmlNode *nvpair = NULL;
	xmlNode *configuration = NULL;
	xmlNode *crm_config = NULL;
	xmlNode *cluster_property = NULL;

	::qpid::sys::Mutex::ScopedLock _lock(xml_lock);
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

	_pe = xmlNewDoc(BAD_CAST "1.0");

	/* header gumf */
	cib = xmlNewNode(NULL, BAD_CAST "cib");
	xmlDocSetRootElement(_pe, cib);
	xmlNewProp(cib, BAD_CAST "admin_epoch", BAD_CAST "0");
	xmlNewProp(cib, BAD_CAST "epoch", BAD_CAST "0");
	xmlNewProp(cib, BAD_CAST "num_updates", BAD_CAST "1");
	xmlNewProp(cib, BAD_CAST "have-quorum", BAD_CAST "false");
	xmlNewProp(cib, BAD_CAST "dc-uuid", BAD_CAST "0");
	xmlNewProp(cib, BAD_CAST "remote-tls-port", BAD_CAST "0");
	xmlNewProp(cib, BAD_CAST "validate-with", BAD_CAST "pacemaker-1.0");

	configuration = xmlNewChild(cib, NULL, BAD_CAST "configuration", NULL);
	crm_config = xmlNewChild(configuration, NULL, BAD_CAST "crm_config", NULL);

	cluster_property = xmlNewChild(crm_config, NULL, BAD_CAST "cluster_property_set", NULL);
	xmlNewProp(cluster_property, BAD_CAST "id", BAD_CAST "no-stonith");
	nvpair = xmlNewChild(cluster_property, NULL, BAD_CAST "nvpair", NULL);
	xmlNewProp(nvpair, BAD_CAST "id", BAD_CAST "opt-no-stonith");
	xmlNewProp(nvpair, BAD_CAST "name", BAD_CAST "stonith-enabled");
	xmlNewProp(nvpair, BAD_CAST "value", BAD_CAST "false");

	cluster_property = xmlNewChild(crm_config, NULL, BAD_CAST "cluster_property_set", NULL);
	xmlNewProp(cluster_property, BAD_CAST "id", BAD_CAST "bootstrap-options");
	nvpair = xmlNewChild(cluster_property, NULL, BAD_CAST "nvpair", NULL);
	xmlNewProp(nvpair, BAD_CAST "id", BAD_CAST "opt-not-symetric");
	xmlNewProp(nvpair, BAD_CAST "name", BAD_CAST "symetric_cluster");
	xmlNewProp(nvpair, BAD_CAST "value", BAD_CAST "false");

	nvpair = xmlNewChild(cluster_property, NULL, BAD_CAST "nvpair", NULL);
	xmlNewProp(nvpair, BAD_CAST "id", BAD_CAST "opt-no-quorum-policy");
	xmlNewProp(nvpair, BAD_CAST "name", BAD_CAST "no-quorum-policy");
	xmlNewProp(nvpair, BAD_CAST "value", BAD_CAST "ignore");

	dep_node = xmlDocGetRootElement(_config);
	for (cur_node = dep_node->children; cur_node;
	     cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			qb_log(LOG_DEBUG, "hostname: %s", cur_node->name);
			if (strcmp((char*)cur_node->name, "assemblies") == 0) {
				assemblies2nodes(configuration, cur_node->children);
			} else if (strcmp((char*)cur_node->name, "services") == 0) {
				services2resources(configuration, cur_node->children);
			}
		}
	}
	crm_config = xmlNewChild(configuration, NULL, BAD_CAST "constraints", NULL);
}

static gboolean
_status_timeout(gpointer data)
{
	Deployable *d = (Deployable *)data;
	d->process();
	return FALSE;
}

static uint32_t
resource_execute(struct pe_operation *op)
{
	Assembly *a;
	Deployable *d = (Deployable *)op->user_data;
	string name = op->hostname;

	a = d->assembly_get(name);
	assert(a != NULL);
	return a->resource_execute(op);
}

Assembly*
Deployable::assembly_get(std::string& hostname)
{
	// FIXME we need to convert from hostname to uuid
	// atm they are the same but they won't be forever.
	return _assemblies[hostname];
}

void
Deployable::process(void)
{
	map<string, Assembly*>::iterator iter;
	xmlNode * cur_node = NULL;
	xmlNode * pe_root = NULL;
	xmlNode * status = NULL;
	Assembly *a;
	::qpid::sys::Mutex::ScopedLock _lock(xml_lock);

	if (_pe == NULL) {
		return;
	}

	pe_root = xmlDocGetRootElement(_pe);
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

	for (iter = _assemblies.begin(); iter != _assemblies.end(); iter++) {
		a = iter->second;
		a->insert_status(status);
	}

	pe_process_state(pe_root, resource_execute, this);

	_status_changed = false;
}

void
Deployable::status_changed(void)
{
	qb_log(LOG_DEBUG, "status_changed current:%d", _status_changed);

	if (!_status_changed) {
		_status_changed = true;
		// set in change and start timer
		g_timeout_add(1000,
			      _status_timeout,
			      this);
	} else {
		// restart timer
	}
}

int32_t
Deployable::assembly_add(string& name, string& uuid, string& ip)
{
	Assembly *a = _assemblies[uuid];
	if (a) {
		// don't want duplicates
		return -1;
	}

	try {
		a = new Assembly(this, name, uuid, ip);
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

