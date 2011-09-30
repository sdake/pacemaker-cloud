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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>

#include <string>

#include <qb/qblog.h>
#include <cpe_httpd.h>
#include <cpe_impl.h>

#define PORT            8888
#define POSTBUFFERSIZE  512
#define MAXNAMESIZE     20
#define MAXANSWERSIZE   512

#define GET             0
#define POST            1

struct connection_info_struct {
	CpeHttpd* cpe_httpd;
	int connectiontype;
	char *answerstring;
	struct MHD_PostProcessor *postprocessor;
};

const char *askpage = "<deployments><body>\
                       What's your name, Sir?<br>\
                       <form action=\"/namepost\" method=\"post\">\
                       <input name=\"name\" type=\"text\"\
                       <input type=\"submit\" value=\" Send \"></form>\
                       </body></html>";

const char *started_page =
    "<deployment id=\"%s\" href=\"pacemaker-cloud/api/%s\"></deployment>";

const char *errorpage =
    "<html><body>This doesn't seem to be right.</body></html>";


static int
send_page(struct MHD_Connection *connection,
	  const char *page,
	  const char *mimetype,
	  int status_code)
{
	int ret;
	struct MHD_Response *response;

	response =
		MHD_create_response_from_buffer(strlen(page), (void *)page,
						MHD_RESPMEM_PERSISTENT);
	if (!response) {
		return MHD_NO;
	}

	if (mimetype) {
		MHD_add_response_header(response,
					MHD_HTTP_HEADER_CONTENT_TYPE,
					mimetype);
		// MHD_HTTP_HEADER_CONTENT_LOCATION
	}
	ret = MHD_queue_response(connection, status_code, response);
	MHD_destroy_response(response);

	return ret;
}

static int
iterate_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
	     const char *filename, const char *content_type,
	     const char *transfer_encoding, const char *data, uint64_t off,
	     size_t size)
{
	struct connection_info_struct *con_info = (struct connection_info_struct *)coninfo_cls;
	CpeHttpd* cpe_httpd = con_info->cpe_httpd;

	if (0 == strcmp(key, "deployment")) {
		xmlNode *dep = NULL;
		xmlNode *node = NULL;
		std::string dep_uuid;
		xmlDocPtr doc = xmlParseMemory(data, size);
		dep = xmlDocGetRootElement(doc);

		for (node = dep->children; node;
		     node = node->next) {
			if (node->type == XML_ELEMENT_NODE) {
				if (strcmp((char*)node->name, "deployment") == 0) {
					dep_uuid = (char*)xmlGetProp(node, BAD_CAST "id");
				}
			}
		}
		xmlFreeDoc(doc);

		if (dep_uuid.length() > 0) {
			char *answerstring;
			answerstring = (char*)malloc(MAXANSWERSIZE);
			if (!answerstring)
				return MHD_NO;
			qb_log(LOG_NOTICE, "Starting %s %s", key,
			       dep_uuid.c_str());

			if (cpe_httpd->impl_get()->dep_start(dep_uuid) == 0) {
				snprintf(answerstring, MAXANSWERSIZE, started_page,
					 dep_uuid.c_str(), dep_uuid.c_str());
				con_info->answerstring = answerstring;
			} else {
				con_info->answerstring = NULL;
			}
		} else {
			con_info->answerstring = NULL;
		}

		return MHD_NO;
	}

	return MHD_YES;
}

static void
request_completed(void *cls, struct MHD_Connection *connection,
		  void **con_cls, enum MHD_RequestTerminationCode toe)
{
	struct connection_info_struct *con_info = *(struct connection_info_struct **)con_cls;
	CpeHttpd* cpe_httpd = (CpeHttpd*)cls;

	if (NULL == con_info) {
		return;
	}

	if (con_info->connectiontype == POST) {
		MHD_destroy_post_processor(con_info->postprocessor);
		if (con_info->answerstring) {
			free(con_info->answerstring);
		}
	}

	free(con_info);
	*con_cls = NULL;
}

static int
answer_to_connection(void *cls, struct MHD_Connection *connection,
		     const char *url, const char *method,
		     const char *version, const char *upload_data,
		     size_t * upload_data_size, void **con_cls)
{
	CpeHttpd* cpe_httpd = (CpeHttpd*)cls;

	qb_log(LOG_DEBUG, "got a %s to %s", method, url);

	if (strncmp(url, "/pacemaker-cloud/api",
		    strlen("/pacemaker-cloud/api")) != 0) {
		return send_page(connection, errorpage, NULL, MHD_HTTP_NOT_FOUND);
	}

	if (NULL == *con_cls) {
		struct connection_info_struct *con_info;

		con_info = (struct connection_info_struct*)malloc(sizeof(struct connection_info_struct));
		if (NULL == con_info) {
			return MHD_NO;
		}
		con_info->answerstring = NULL;
		con_info->cpe_httpd = cpe_httpd;

		if (strcmp(method, MHD_HTTP_METHOD_POST) == 0) {
			con_info->postprocessor =
			    MHD_create_post_processor(connection,
						      POSTBUFFERSIZE,
						      iterate_post,
						      (void *)con_info);

			if (NULL == con_info->postprocessor) {
				free(con_info);
				return MHD_NO;
			}

			con_info->connectiontype = POST;
		} else {
			con_info->connectiontype = GET;
		}
		*con_cls = (void *)con_info;

		return MHD_YES;
	}

	if (strcmp(method, MHD_HTTP_METHOD_GET) == 0) {

		// TODO ...
		std::list<std::string> l;
		cpe_httpd->impl_get()->dep_list(&l);

		return send_page(connection, askpage,
				 "application/xml", MHD_HTTP_OK);
	}
	if (strcmp(method, MHD_HTTP_METHOD_DELETE) == 0) {
		// TODO ...
		return send_page(connection, NULL, NULL, MHD_HTTP_NO_CONTENT);
	}

	if (strcmp(method, MHD_HTTP_METHOD_POST) == 0) {
		struct connection_info_struct *con_info = *(struct connection_info_struct **)con_cls;

		if (*upload_data_size != 0) {
			MHD_post_process(con_info->postprocessor, upload_data,
					 *upload_data_size);
			*upload_data_size = 0;

			return MHD_YES;
		} else if (NULL != con_info->answerstring) {
			return send_page(connection, con_info->answerstring,
					 "application/xml", MHD_HTTP_CREATED);
		} else {
			qb_log(LOG_DEBUG, "answerstring NULL -> errorpage", method);
		}
	}
	qb_log(LOG_DEBUG, "\"%s\" -> errorpage", method);
	return send_page(connection, errorpage, NULL, MHD_HTTP_BAD_REQUEST);
}

CpeHttpd::CpeHttpd() : daemon(NULL)
{
}

CpeHttpd::~CpeHttpd()
{
	if (daemon) {
		MHD_stop_daemon(daemon);
	}
}

void
CpeHttpd::run(void)
{
	daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY,
				  PORT, NULL, NULL,
				  &answer_to_connection, this,
				  MHD_OPTION_NOTIFY_COMPLETED,
				  request_completed, this,
				  MHD_OPTION_END);
}

void CpeHttpd::impl_set(CpeImpl *impl)
{
	this->impl = impl;
}
