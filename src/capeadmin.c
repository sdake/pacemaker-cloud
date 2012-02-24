/*
 * Copyright (C) 2012 Red Hat, Inc.
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

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif /* HAVE_FCNTL_H */

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif /* HAVE_SYS_UN_H */

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>
#include <libxml/parser.h>

#include "cape.h"


#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX    108
#endif /* UNIX_PATH_MAX */

static int sockfd = -1;

static void
event_spammer(void *data, size_t len)
{
	ssize_t rc;
	ssize_t  processed = 0;

	if (sockfd < 0) {
		return;
	}
retry_send:
	rc = send(sockfd, data, len, MSG_NOSIGNAL);
	if (rc == -1) {
		qb_perror(LOG_ERR, "event_spammer");
		return;
	}

	processed += rc;
	if (processed != len) {
		goto retry_send;
	}
}

void
cape_admin_event_send(const char *app,
		      struct assembly *a,
		      struct resource *r,
		      const char *state,
		      const char *reason)
{
	xmlNode *xevent;
	xmlNode *xapp;
	xmlNode *xnode;
	xmlNode *xresource;
	xmlNode *ev_level;
	xmlChar *mem_buf;
	int mem_size;
	xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");

	xevent = xmlNewNode(NULL, BAD_CAST "event");
	xmlDocSetRootElement(doc, xevent);

	xapp = xmlNewChild(xevent, NULL, BAD_CAST "application", NULL);
	xmlNewProp(xapp, BAD_CAST "name", BAD_CAST app);
	ev_level = xapp;
	if (a) {
		xnode = xmlNewChild(xapp, NULL, BAD_CAST "node", NULL);
		xmlNewProp(xnode, BAD_CAST "name", BAD_CAST a->name);
		ev_level = xnode;
	}
	if (a && r) {
		xresource = xmlNewChild(xnode, NULL, BAD_CAST "resource", NULL);
		xmlNewProp(xresource, BAD_CAST "name", BAD_CAST r->name);
		ev_level = xresource;
	}
	xmlNewChild(ev_level, NULL, BAD_CAST "state",  BAD_CAST state);
	xmlNewChild(ev_level, NULL, BAD_CAST "reason", BAD_CAST reason);

	xmlDocDumpMemory(doc, &mem_buf, &mem_size);

	event_spammer(mem_buf, mem_size);

	xmlFree(mem_buf);
	xmlFreeDoc(doc);
}

static int32_t
_fd_nonblock_cloexec_set(int32_t fd)
{
	int32_t res = 0;
	int32_t oldflags = fcntl(fd, F_GETFD, 0);

	if (oldflags < 0) {
		oldflags = 0;
	}
	oldflags |= FD_CLOEXEC;
	res = fcntl(fd, F_SETFD, oldflags);
	if (res == -1) {
		res = -errno;
		qb_perror(LOG_ERR,
			       "Could not set close-on-exit on fd:%d", fd);
		return res;
	}

	res = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		res = -errno;
		qb_log(LOG_ERR, "Could not set non-blocking on fd:%d", fd);
	}

	return res;
}

static int32_t
_unix_sock_connect(const char *socket_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un address;
	int32_t res = 0;

	request_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (request_fd == -1) {
		return -errno;
	}
	res = _fd_nonblock_cloexec_set(request_fd);
	if (res < 0) {
		goto error_connect;
	}

	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;

	snprintf(address.sun_path, UNIX_PATH_MAX - 1, "%s", socket_name);
	if (connect(request_fd, (struct sockaddr *)&address,
		    sizeof(address)) == -1) {
		res = -errno;
		goto error_connect;
	}

	*sock_pt = request_fd;
	return 0;

error_connect:
	close(request_fd);
	*sock_pt = -1;

	return res;
}

int32_t
cape_admin_init(void)
{
	return _unix_sock_connect("pacemaker-cloud-cped", &sockfd);
}

void
cape_admin_fini(void)
{
   close(sockfd);
}

