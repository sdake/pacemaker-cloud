/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Authors: Steven Dake <sdake@redhat.com>
 *          Angus Salkeld <asalkeld@redhat.com>
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
#include <sys/epoll.h>
#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <qb/qbmap.h>
#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "cape.h"
#include "trans.h"

static void assembly_healthcheck(void *data);

enum ssh_state {
	SSH_SESSION_INIT = 0,
	SSH_SESSION_STARTUP = 1,
	SSH_KEEPALIVE_CONFIG = 2,
	SSH_USERAUTH_PUBLICKEY_FROMFILE = 3,
	SSH_CONNECTED = 4
};

enum ssh_exec_state {
	SSH_CHANNEL_OPEN = 0,
	SSH_CHANNEL_EXEC = 1,
	SSH_CHANNEL_READ = 2,
	SSH_CHANNEL_SEND_EOF = 3,
	SSH_CHANNEL_WAIT_EOF = 4,
	SSH_CHANNEL_CLOSE = 5,
	SSH_CHANNEL_WAIT_CLOSED = 6,
	SSH_CHANNEL_FREE = 7
};

struct ssh_operation {
	int ssh_rc;
	struct assembly *assembly;
	struct resource *resource;
	struct qb_list_head list;
	struct pe_operation *op;
	qb_loop_timer_handle ssh_timer;
	enum ssh_exec_state ssh_exec_state;
	qb_loop_job_dispatch_fn completion_func;
	LIBSSH2_CHANNEL *channel;
	int failed;
	/*
	 * 26 = "systemctl [start|stop|status] .service null-terminator
	 */
	char command[RESOURCE_NAME_MAX + 26];
};

struct ta_ssh {
	int fd;
	enum ssh_state ssh_state;
	qb_loop_timer_handle keepalive_timer;
	LIBSSH2_SESSION *session;
	struct sockaddr_in sin;
	struct qb_list_head ssh_op_head;
};


int ssh_init_rc = -1;

static void ssh_op_complete(struct ssh_operation *ssh_op)
{
	qb_loop_timer_del(NULL, ssh_op->ssh_timer);
	qb_list_del(&ssh_op->list);
	if (ssh_op->failed == 0) {
		ssh_op->completion_func(ssh_op);
	}
	if (ssh_op->op) {
		pe_resource_unref(ssh_op->op);
	}
	free(ssh_op);
}

static void assembly_ssh_exec(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	struct ta_ssh *ta_ssh = (struct ta_ssh *)ssh_op->assembly->transport_assembly;
	int rc;
	char buffer[4096];
	ssize_t rc_read;

	qb_enter();

	switch (ssh_op->ssh_exec_state) {
	case SSH_CHANNEL_OPEN:
		ssh_op->channel = libssh2_channel_open_session(ta_ssh->session);
		if (ssh_op->channel == NULL) {
			rc = libssh2_session_last_errno(ta_ssh->session);
			if (rc == LIBSSH2_ERROR_EAGAIN) {
				goto job_repeat_schedule;
			}
			qb_log(LOG_NOTICE,
				"open session failed %d\n", rc);
			qb_leave();
			return;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_EXEC;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_EXEC:
		rc = libssh2_channel_exec(ssh_op->channel, ssh_op->command);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_exec failed %d\n", rc);
			ssh_op->failed = 1;
			goto channel_free;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_READ;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_READ:
		rc_read = libssh2_channel_read(ssh_op->channel, buffer, sizeof(buffer));
		if (rc_read == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc_read < 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_read failed %d\n", rc);
			ssh_op->failed = 1;
			goto channel_free;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_SEND_EOF;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_SEND_EOF:
		rc = libssh2_channel_send_eof(ssh_op->channel);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_send_eof failed %d\n", rc);
			ssh_op->failed = 1;
			goto channel_free;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_WAIT_EOF;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_WAIT_EOF:
		rc = libssh2_channel_wait_eof(ssh_op->channel);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_wait_eof failed %d\n", rc);
			ssh_op->failed = 1;
			goto channel_free;
			return;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_CLOSE;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_CLOSE:
		rc = libssh2_channel_close(ssh_op->channel);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel close failed %d\n", rc);
			ssh_op->failed = 1;
			goto channel_free;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_WAIT_CLOSED;

		/*
                 * no break here is intentional
		 */


	case SSH_CHANNEL_WAIT_CLOSED:
		rc = libssh2_channel_wait_closed(ssh_op->channel);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_wait_closed failed %d\n", rc);
			ssh_op->failed = 1;
			goto channel_free;
		}
		/*
		 * No ERROR_EAGAIN returned by following call
		 */
		ssh_op->ssh_rc = libssh2_channel_get_exit_status(ssh_op->channel);

		/*
                 * no break here is intentional
		 */

channel_free:
		ssh_op->ssh_exec_state = SSH_CHANNEL_FREE;

	case SSH_CHANNEL_FREE:
		rc = libssh2_channel_free(ssh_op->channel);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_free failed %d\n", rc);
		}
		break;

	default:
		assert(0);
	} /* switch */

	ssh_op_complete(ssh_op);

	qb_leave();

	return;

job_repeat_schedule:
	qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);

	qb_leave();
}

static void ssh_timeout(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	struct ta_ssh *ta_ssh = (struct ta_ssh *)ssh_op->assembly->transport_assembly;
	struct qb_list_head *list_temp;
	struct qb_list_head *list;
	struct ssh_operation *ssh_op_del;

	qb_enter();

	qb_log(LOG_NOTICE, "ssh service timeout on assembly '%s'", ssh_op->assembly->name);
	qb_list_for_each_safe(list, list_temp, &ta_ssh->ssh_op_head) {
		ssh_op_del = qb_list_entry(list, struct ssh_operation, list);
		qb_loop_timer_del(NULL, ssh_op_del->ssh_timer);
		qb_loop_job_del(NULL, QB_LOOP_LOW, ssh_op_del,
			assembly_ssh_exec);
		qb_list_del(list);
		qb_log(LOG_NOTICE, "delete ssh operation '%s'", ssh_op_del->command);
	}

	node_state_changed(ssh_op->assembly, NODE_STATE_FAILED);

	qb_leave();
}

static void ssh_keepalive_send(void *data)
{
	struct ta_ssh *ta_ssh = (struct ta_ssh *)data;
	int seconds_to_next;

	qb_enter();

	libssh2_keepalive_send(ta_ssh->session, &seconds_to_next);
	qb_loop_timer_add(NULL, QB_LOOP_LOW, seconds_to_next * 1000 * QB_TIME_NS_IN_MSEC,
		ta_ssh, ssh_keepalive_send, &ta_ssh->keepalive_timer);

	qb_leave();
}

static void ssh_assembly_connect(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	struct ta_ssh *ta_ssh = (struct ta_ssh *)assembly->transport_assembly;
	char name[PATH_MAX];
	char name_pub[PATH_MAX];
	int rc;

	qb_enter();
	switch (ta_ssh->ssh_state) {
	case SSH_SESSION_INIT:
		ta_ssh->session = libssh2_session_init();
		if (ta_ssh->session == NULL) {
			goto job_repeat_schedule;
		}
printf ("setting ta_ssh->session %p\n", ta_ssh);

		libssh2_session_set_blocking(ta_ssh->session, 0);
		ta_ssh->ssh_state = SSH_SESSION_STARTUP;

		/*
                 * no break here is intentional
		 */

	case SSH_SESSION_STARTUP:
		rc = libssh2_session_startup(ta_ssh->session, ta_ssh->fd);
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc != 0) {
			qb_log(LOG_NOTICE,
				"session startup failed %d\n", rc);
			goto error;
		}

		/*
                 * no break here is intentional
		 */
		ta_ssh->ssh_state = SSH_KEEPALIVE_CONFIG;

	case SSH_KEEPALIVE_CONFIG:
		libssh2_keepalive_config(ta_ssh->session, 1, KEEPALIVE_TIMEOUT);
		qb_loop_job_add(NULL, QB_LOOP_LOW, ta_ssh, ssh_keepalive_send);

		/*
                 * no break here is intentional
		 */
		ta_ssh->ssh_state = SSH_USERAUTH_PUBLICKEY_FROMFILE;

	case SSH_USERAUTH_PUBLICKEY_FROMFILE:
		snprintf (name, PATH_MAX, "/var/lib/pacemaker-cloud/keys/%s",
			assembly->name);
		snprintf (name_pub, PATH_MAX, "/var/lib/pacemaker-cloud/keys/%s.pub",
			assembly->name);
		rc = libssh2_userauth_publickey_fromfile(ta_ssh->session,
			"root", name_pub, name, "");
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc) {
			qb_log(LOG_ERR,
				"Authentication by public key for '%s' failed\n",
				assembly->name);
		} else {
			qb_log(LOG_NOTICE,
				"Authentication by public key for '%s' successful\n",
				assembly->name);
		}
		assembly_healthcheck(assembly);

		node_state_changed(assembly, NODE_STATE_RUNNING);
		ta_ssh->ssh_state = SSH_CONNECTED;

	case SSH_CONNECTED:
		break;
	}
error:
	qb_leave();
	return;

job_repeat_schedule:
	qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, ssh_assembly_connect);
	qb_leave();
}

static void
ssh_op_delete(struct ssh_operation *ssh_op)
{
	qb_enter();
	qb_loop_job_del(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
	qb_leave();
}

static void
ssh_nonblocking_exec(struct assembly *assembly,
	struct resource *resource, struct pe_operation *op,
	void (*completion_func)(void *),
	char *format, ...)
{
	va_list ap;
	struct ssh_operation *ssh_op;
	struct ta_ssh *ta_ssh;

	qb_enter();
	ssh_op = calloc(1, sizeof(struct ssh_operation));

	/*
	 * 26 = "systemctl [start|stop|status] .service null-terminator
	 */
	va_start(ap, format);
	vsnprintf(ssh_op->command, RESOURCE_NAME_MAX + 26, format, ap);
	va_end(ap);

	qb_log(LOG_NOTICE, "ssh_exec for assembly '%s' command '%s'",
		assembly->name, ssh_op->command);

	ssh_op->assembly = assembly;
	ssh_op->ssh_rc = 0;
	ssh_op->failed = 0;
	ssh_op->op = op;
	ssh_op->ssh_exec_state = SSH_CHANNEL_OPEN;
	ssh_op->resource = resource;
	ssh_op->completion_func = completion_func;
	qb_list_init(&ssh_op->list);
	ta_ssh = ssh_op->assembly->transport_assembly;
	qb_list_add_tail(&ssh_op->list, &ta_ssh->ssh_op_head);

	if (op) {
		pe_resource_ref(ssh_op->op);
	}

	qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
	qb_loop_timer_add(NULL, QB_LOOP_LOW,
		SSH_TIMEOUT * QB_TIME_NS_IN_MSEC,
		ssh_op, ssh_timeout, &ssh_op->ssh_timer);
	qb_leave();
}


static void assembly_healthcheck_completion(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;

	qb_enter();

	qb_log(LOG_NOTICE, "assembly_healthcheck_completion for assembly '%s'", ssh_op->assembly->name);
	if (ssh_op->ssh_rc != 0) {
		qb_log(LOG_NOTICE, "assembly healthcheck failed %d\n", ssh_op->ssh_rc);
		ta_del(ssh_op->assembly->transport_assembly);
		ssh_op_delete(ssh_op);
		node_state_changed(ssh_op->assembly, NODE_STATE_FAILED);
		//free(ssh_op);
		qb_leave();
		return;
	}

	/*
	 * Add a healthcheck if asssembly is still running
	 */
	if (ssh_op->assembly->instance_state == NODE_STATE_RUNNING) {
		qb_log(LOG_NOTICE, "adding a healthcheck timer for assembly '%s'", ssh_op->assembly->name);
		qb_loop_timer_add(NULL, QB_LOOP_HIGH,
			HEALTHCHECK_TIMEOUT * QB_TIME_NS_IN_MSEC, ssh_op->assembly,
			assembly_healthcheck, &ssh_op->assembly->healthcheck_timer);
	}

	qb_leave();
}

static void assembly_healthcheck(void *data)
{
	struct assembly *assembly = (struct assembly *)data;

	qb_enter();

	ssh_nonblocking_exec(assembly, NULL, NULL,
		assembly_healthcheck_completion,
		"uptime");

	qb_leave();
}

static void connect_execute(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	struct ta_ssh *ta_ssh = (struct ta_ssh *)assembly->transport_assembly;
	int rc;

	qb_enter();

	rc = connect(ta_ssh->fd, (struct sockaddr*)(&ta_ssh->sin),
		sizeof (struct sockaddr_in));
	if (rc == 0) {
		qb_log(LOG_NOTICE, "Connected to assembly '%s'",
			assembly->name);
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, ssh_assembly_connect);
	} else {
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, connect_execute);
	}

	qb_leave();
}

void ta_del(void *ta)
{
	struct ta_ssh *ta_ssh = (struct ta_ssh *)ta;

	qb_enter();

	qb_loop_timer_del(NULL, ta_ssh->keepalive_timer);

	qb_leave();
}

static void resource_action_completion_cb(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	enum ocf_exitcode pe_rc;

	qb_enter();

	pe_rc = pe_resource_ocf_exitcode_get(ssh_op->op, ssh_op->ssh_rc);

	resource_action_completed(ssh_op->op, pe_rc);
	ssh_op_delete(ssh_op);

	qb_leave();
}

void
ta_resource_action(struct assembly * a,
		   struct resource *r,
		   struct pe_operation *op)
{
	qb_enter();

	if (strcmp(op->method, "monitor") == 0) {
		ssh_nonblocking_exec(a, r, op,
			resource_action_completion_cb,
			"systemctl status %s.service",
			op->rtype);
	} else {
		ssh_nonblocking_exec(a, r, op,
			resource_action_completion_cb,
			"systemctl %s %s.service",
			op->method, op->rtype);
	}

	qb_leave();
}

void*
ta_connect(struct assembly * a)
{
	unsigned long hostaddr;
        struct ta_ssh *ta_ssh;

	qb_enter();

	if (ssh_init_rc != 0) {
		ssh_init_rc = libssh2_init(0);
	}
	assert(ssh_init_rc == 0);

	ta_ssh = calloc(1, sizeof(struct ta_ssh));
	a->transport_assembly = ta_ssh;

	hostaddr = inet_addr(a->address);
	ta_ssh->fd = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(ta_ssh->fd, F_SETFL, O_NONBLOCK);
	ta_ssh->sin.sin_family = AF_INET;
	ta_ssh->sin.sin_port = htons(22);
	ta_ssh->sin.sin_addr.s_addr = hostaddr;
	ta_ssh->ssh_state = SSH_SESSION_INIT;
	qb_list_init(&ta_ssh->ssh_op_head);

	qb_log(LOG_NOTICE, "Connection in progress to assembly '%s'",
		a->name);

	qb_loop_job_add(NULL, QB_LOOP_LOW, a, connect_execute);

	qb_leave();
}

