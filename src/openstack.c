/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Authors: Steven Dake <sdake@redhat.com>
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

#include <glib.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <assert.h>
#include <curl/curl.h>
#include <memory.h>
#include <libxml2/libxml/parser.h>
#include <qb/qbutil.h>

#include "config.h"

#include "cape.h"
#include "trans.h"

/*
 * curl_callback data structures
 */
struct instance_state_get_data {
	void (*completion_func)(char *, char *, void *);
	void *data;
	char *instance_id;
};

struct image_id_get_data {
	void (*completion_func)(char *, void *);
	void *data;
	char *image_name;
};

struct instance_create_data {
	void (*completion_func)(char *, void *);
	void *data;
	char *image_name;
	char *instance_id;
};
	
/*
 * Internal Implementation
 */
static size_t instance_state_get_curl_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
	struct instance_state_get_data *instance_state_get_data = (struct instance_state_get_data *)data;
	xmlDocPtr xml;
	xmlNodePtr cur_node;
	xmlChar *status = NULL;
	xmlChar *ip_addr = NULL;

	xml = xmlReadMemory(ptr, nmemb, "test.com", NULL,
		XML_PARSE_NOENT | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	cur_node = xmlDocGetRootElement(xml);
	status = xmlGetProp(cur_node, (const xmlChar *)"status");
	/*
	 * Find private address
	 * UGH
	 */
	if (strcmp((char *)status, "ACTIVE") == 0) {
		for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
			if (strcmp((char *)cur_node->name, "addresses") == 0) {
				if (cur_node->children) {
					for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
						if (strcmp((char *)cur_node->name, "private") == 0) {
							for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
								if (strcmp((char *)cur_node->name, "ip") == 0) {
									ip_addr = xmlGetProp(cur_node, (const xmlChar *)"addr");
									goto done;
								}
							}
						}
					}
				}
			}
		}
	}
done:
	if (status) {
		instance_state_get_data->completion_func((char *)status, (char *)ip_addr, instance_state_get_data->data);
	}
	free(instance_state_get_data);
	xmlFree(xml);
	return 0;
}

static size_t image_id_get_curl_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
	struct image_id_get_data *image_id_get = (struct image_id_get_data *)data;
	xmlDocPtr xml;
	xmlNodePtr cur_node;
	xmlChar *name;
	xmlChar *id = NULL;

	xml = xmlReadMemory(ptr, nmemb, "test.com", NULL,
		XML_PARSE_NOENT | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	cur_node = xmlDocGetRootElement(xml);

	for (cur_node = cur_node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			name = xmlGetProp(cur_node, (const xmlChar *)"name");
			if (strcmp((char *)name, image_id_get->image_name) == 0) {
				id = xmlGetProp(cur_node, (const xmlChar *)"id");
				break;
			}
		}
	}
	if (id) {
		image_id_get->completion_func((char *)id, image_id_get->data);
	}
	free(image_id_get);
	xmlFree(xml);
	return 0;
}

static size_t instance_create_curl_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
	struct instance_create_data *instance_create_data = (struct instance_create_data *)data;
	xmlDocPtr xml;
	xmlNodePtr cur_node;
	xmlChar *id = NULL;

	xml = xmlReadMemory(ptr, nmemb, "test.com", NULL,
		XML_PARSE_NOENT | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	cur_node = xmlDocGetRootElement(xml);
	id = xmlGetProp(cur_node, (const xmlChar *)"id");
	if (id) {
		instance_create_data->completion_func((char *)id, instance_create_data->data);
	}
	free(instance_create_data);
	xmlFree(xml);
	return 0;
}


/*
 * External API
 */
void instance_state_get(char *instance_id,
	void (*completion_func)(char *, char *, void *),
	void *data)
{
	struct curl_slist *headers = NULL;
	CURL *curl;
	struct instance_state_get_data *instance_state_get_data;
	char url[1024];

	instance_state_get_data = calloc(1, sizeof (struct instance_state_get_data));
	instance_state_get_data->completion_func = completion_func;
	instance_state_get_data->data = data;
	instance_state_get_data->instance_id = instance_id;
	sprintf (url, "http://localhost:8774/v1.0/servers/%s", instance_id);

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERNAME, "sdake");
	curl_easy_setopt(curl, CURLOPT_PASSWORD, "sdake");
	headers = curl_slist_append(headers, "Accept: application/xml");
	headers = curl_slist_append(headers, "X-Auth_token: sdake:dep-wp");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, instance_state_get_curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, instance_state_get_data);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

	curl_easy_perform(curl);
}

void image_id_get(char *image_name,
	void (*completion_func)(char *, void *),
	void *data)
{
	struct curl_slist *headers = NULL;
	CURL *curl;
	struct image_id_get_data *image_id_get_data;

	image_id_get_data = calloc(1, sizeof(struct image_id_get_data));
	image_id_get_data->completion_func = completion_func;
	image_id_get_data->data = data;
	image_id_get_data->image_name = image_name;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8774/v1.0/images");
	curl_easy_setopt(curl, CURLOPT_USERNAME, "sdake");
	curl_easy_setopt(curl, CURLOPT_PASSWORD, "sdake");
	headers = curl_slist_append(headers, "Accept: application/xml");
	headers = curl_slist_append(headers, "X-Auth_token: sdake:dep-wp");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 0);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, image_id_get_curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, image_id_get_data);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

	curl_easy_perform(curl);
}

void instance_create_from_image_id(char *image_id,
	void (*completion_func)(char *instance_id, void *data),
	void *data)
{
	struct curl_slist *headers = NULL;
	CURL *curl;
	struct instance_create_data *instance_create_data;

	char command[1024];
	sprintf (command, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><server xmlns=\"http://docs.rackspacecloud.com/servers/api/v1.0\" name=\"test\" imageId=\"%s\" flavorId=\"1\"> </server>", image_id);
	instance_create_data = calloc(1, sizeof(struct instance_create_data));
	instance_create_data->completion_func = completion_func;
	instance_create_data->data = data;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8774/v1.0/servers");
	curl_easy_setopt(curl, CURLOPT_USERNAME, "sdake");
	curl_easy_setopt(curl, CURLOPT_PASSWORD, "sdake");
	headers = curl_slist_append(headers, "Accept: application/xml");
	headers = curl_slist_append(headers, "Content-Type: application/xml");
	headers = curl_slist_append(headers, "X-Auth_token: sdake:dep-wp");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, instance_create_curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, instance_create_data);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, command);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(command));
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_perform(curl);
}
