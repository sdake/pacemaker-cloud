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

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <assert.h>
#include <qb/qblog.h>

#include "config_loader.h"
#include "deployable.h"
#include "assembly.h"

using namespace std;

Deployable::Deployable(std::string& uuid)
{
	_name = "";
	_uuid = uuid;
	reload();
}

Deployable::~Deployable()
{
	map<string, Assembly*>::iterator kill;
	map<string, Assembly*>::iterator iter;
	Assembly *a;

	for (iter = assemblies.begin(); iter != assemblies.end(); ) {
		kill = iter;
		a = kill->second;
		iter++;
		assemblies.erase(kill);
		a->stop();
	}
}

void
Deployable::reload(void)
{
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr xpathCtx;
	xmlXPathObjectPtr xpathObj;
	int32_t rc;
	int size;
	int i;
	string ass_name;
	string ass_uuid;
	string ass_ip;

	rc = config_get(_uuid, &doc);
	if (rc != 0) {
		qb_log(LOG_ERR, "unable to load XML config file");
		// O, crap
		// try again later?
		return;
	}

	xmlInitParser();

	/* Create xpath evaluation context */
	xpathCtx = xmlXPathNewContext(doc);
	if (xpathCtx == NULL) {
		qb_log(LOG_ERR, "unable to create new XPath context");
		xmlFreeDoc(doc);
		return;
	}

	/* Evaluate xpath expression */
	xpathObj = xmlXPathEvalExpression(BAD_CAST "/deployable/assemblies/*", xpathCtx);
	if (xpathObj == NULL) {
		qb_log(LOG_ERR, "unable to evaluate xpath expression");
		xmlXPathFreeContext(xpathCtx);
		xmlFreeDoc(doc);
		return;
	}

	size = (xpathObj->nodesetval) ? xpathObj->nodesetval->nodeNr : 0;
	qb_log(LOG_DEBUG, "parsed config for %s and found %d nodes", _uuid.c_str(), size);

	for (i = 0; i < size; ++i) {
		assert(xpathObj->nodesetval->nodeTab[i]);

		if (xpathObj->nodesetval->nodeTab[i]->type == XML_ELEMENT_NODE) {
			ass_name = (char*)xmlGetProp(xpathObj->nodesetval->nodeTab[i], BAD_CAST "name");
			//ass_uuid = (char*)xmlGetProp(xpathObj->nodesetval->nodeTab[i], BAD_CAST "uuid");
			ass_uuid = ass_name;
			ass_ip = (char*)xmlGetProp(xpathObj->nodesetval->nodeTab[i], BAD_CAST "ipaddr");
			assert(ass_ip.length() > 0);
			assembly_add(ass_name, ass_uuid, ass_ip);
		}
	}

	/* Cleanup */
	xmlXPathFreeObject(xpathObj);
	xmlXPathFreeContext(xpathCtx);

	/* Shutdown libxml */
	xmlCleanupParser();
}

int32_t
Deployable::assembly_add(string& name, string& uuid, string& ip)
{
	Assembly *h = assemblies[uuid];
	if (h) {
		// don't want duplicates
		return -1;
	}

	try {
		h = new Assembly(name, uuid, ip);
	} catch (qpid::types::Exception e) {
		qb_log(LOG_ERR, "Exception creating Assembly %s",
		       e.what());
		delete h;
		return -1;
	}
	assemblies[uuid] = h;
	return 0;
}

int32_t
Deployable::assembly_remove(string& uuid, string& name)
{
	Assembly *h = assemblies[uuid];
	if (h) {
		assemblies.erase(uuid);
		h->stop();
	}
	return 0;
}

#if 0
int assembly_monitor_status(string& host_url)
{
	Assembly *h = hosts[host_url];
	if (h) {
		return h->state_get();
	} else {
		return -1;
	}
}
#endif

