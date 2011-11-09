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

typedef enum {
    GET,
    POST
} connection_type_t;

struct connection_info_struct {
	CpeHttpd* cpe_httpd;
	connection_type_t type;
	char *answerstring;
	xmlParserCtxtPtr ctxt;
};

const char *list_page =
    "<deployments>todo</deployments>";

const char *client_errorpage =
    "<html><body>Invalid input [%s]</body></html>";

const char *server_errorpage =
    "<html><body>Server error [%s]</body></html>";

static bool
set_response_message(struct connection_info_struct *con_info,
		     const char* fmt, ...)
{
	free(con_info->answerstring);

	va_list ap;

	va_start (ap, fmt);
	vasprintf (&(con_info->answerstring), fmt, ap);
	va_end (ap);

	return !!con_info->answerstring;
}

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


static bool
handle_event(xmlDocPtr doc, struct connection_info_struct *con_info)
{
	bool ok = false;
	xmlNode *event = NULL;
	xmlNode *item = NULL;
	xmlNode *node = NULL;
	event = xmlDocGetRootElement(doc);

	const char* event_item = NULL;
	xmlChar* event_item_id = NULL;
	const char* event_state = NULL;

	for (node = event->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE) {
			if (strcmp((char*)node->name, "datetime") == 0) {
				continue;
			}
			event_item = (char*)node->name;
			event_item_id = xmlGetProp(node, BAD_CAST "id");
			item = node;
		}
	}

	for (node = item->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE) {
			if (strcmp((char*)node->name, "state") == 0) {
				if (node->children->type == XML_TEXT_NODE)
					event_state = (const char*)node->children->content;
				break;
			}
		}
	}

	if (event_item && event_item_id && event_state) {
		qb_log(LOG_DEBUG, "Got a %s event for %s %s",
		       event_state, event_item, event_item_id);
		fprintf(stderr, "Got a %s event for %s %s\n",
		       event_state, event_item, event_item_id);

		/* EVENT_STATE should be one of:
		   STARTING, RESTARTING, STARTED, FAILED,
		   STOPPING, STOPPED, ISOLATING, ISOLATED */

		if (strcmp(event_item, "deployment") == 0) {
			/* TODO: before we can enable the following, we
			 * need to convert from or support directly the API XML format,
			 * which differs slightly having "deployment" rather than "deployable"
			 * XML elements for example.
			CpeHttpd* cpe_httpd = con_info->cpe_httpd;
			if (dcpe_httpd->impl_get()->dep_start(dep_uuid) == 0) */
				ok = true;
		} else if (strcmp(event_item, "instance") == 0) {
			ok = true;
		} else {
			qb_log(LOG_DEBUG, "Got an event for an invalid item [%s]", event_item);
		}
	}

	xmlFree (event_item_id);
	return ok;
}


static void
request_completed(void *cls, struct MHD_Connection *connection,
		  void **con_cls, enum MHD_RequestTerminationCode toe)
{
	struct connection_info_struct *con_info = *(struct connection_info_struct **)con_cls;

	if (NULL == con_info) {
		return;
	}

	if (con_info->type == POST) {
		xmlFreeParserCtxt(con_info->ctxt);
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
		     const char *version, const char *data,
		     size_t *size, void **con_cls)
{
	CpeHttpd* cpe_httpd = (CpeHttpd*)cls;

	qb_log(LOG_DEBUG, "got a %s to %s", method, url);

	if (strncmp(url, "/pacemaker-cloud/api",
		    strlen("/pacemaker-cloud/api")) != 0) {
		return send_page(connection, "Not Found", NULL, MHD_HTTP_NOT_FOUND);
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
			con_info->ctxt = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
			if (NULL == con_info->ctxt) {
				free(con_info);
				return MHD_NO;
			}
			con_info->type = POST;
		} else {
			con_info->type = GET;
		}
		*con_cls = (void *)con_info;

		return MHD_YES;
	}

	if (strcmp(method, MHD_HTTP_METHOD_GET) == 0) {

		// TODO ...
		std::list<std::string> l;
		cpe_httpd->impl_get()->dep_list(&l);

		return send_page(connection, list_page,
				 "application/xml", MHD_HTTP_OK);
	}

	if (strcmp(method, MHD_HTTP_METHOD_POST) == 0) {
		struct connection_info_struct *con_info = *(struct connection_info_struct **)con_cls;

		if (*size != 0) {
			int xml_status = xmlParseChunk(con_info->ctxt, data, *size, 0);

			/* NB: Indicate we're finished with data.
			 * This must be done even before responding with error codes.  */
			*size = 0;

			if (xml_status) {
				const char* xml_errmsg = xmlCtxtGetLastError(con_info->ctxt)->message;
				set_response_message(con_info, client_errorpage, xml_errmsg);
				qb_log(LOG_DEBUG, "failed to parse [%s]", xml_errmsg);
			} else {
				return MHD_YES;
			}
		} else {
			bool responded = false;
			int response_status = 0;
			xmlParseChunk(con_info->ctxt, data, 0, 1 /* terminate */);
			xmlDocPtr doc = con_info->ctxt->myDoc;

			if (!con_info->ctxt->wellFormed) {
				const char* xml_errmsg = xmlCtxtGetLastError(con_info->ctxt)->message;
				set_response_message(con_info, client_errorpage, xml_errmsg);
				qb_log(LOG_DEBUG, "failed to parse [%s]", xml_errmsg);
			} else if (handle_event(doc, con_info)) {
				responded = true;
				response_status = send_page(connection, "",
							    "application/xml", MHD_HTTP_OK);
				xmlFreeDoc(doc);
			} else if (!con_info->answerstring) {
				responded = true;
				set_response_message(con_info, server_errorpage, "unknown");
				response_status = send_page(connection, con_info->answerstring,
							    NULL, MHD_HTTP_INTERNAL_SERVER_ERROR);
			}


			if (responded) {
				return response_status;
			}
		}
		qb_log(LOG_DEBUG, "\"%s\" error [%s]", method, con_info->answerstring);
		send_page(connection, con_info->answerstring, NULL, MHD_HTTP_BAD_REQUEST);
                return MHD_YES;
	}
	qb_log(LOG_DEBUG, "\"%s\" -> errorpage", method);
	return send_page(connection, "bad request", NULL, MHD_HTTP_BAD_REQUEST);
}

CpeHttpd::CpeHttpd(int port) : daemon(NULL), port(port)
{
}

CpeHttpd::~CpeHttpd()
{
	if (daemon) {
		MHD_stop_daemon(daemon);
	}
}

bool
CpeHttpd::run(void)
{
	daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY,
				  this->port, NULL, NULL,
				  &answer_to_connection, this,
				  MHD_OPTION_NOTIFY_COMPLETED,
				  request_completed, this,
				  MHD_OPTION_END);
	if (!daemon)
		fprintf(stderr, "Error starting HTTP daemon on localhost:%d\n", this->port);
	else
		fprintf(stderr, "HTTP [info] Listening on localhost:%d\n", this->port);

	return daemon;
}

void CpeHttpd::impl_set(CpeImpl *impl)
{
	this->impl = impl;
}
