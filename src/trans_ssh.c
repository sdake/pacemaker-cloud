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
#include <inttypes.h>
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

static void assembly_ssh_exec(void *data);

enum ssh_state {
	SSH_SESSION_CONNECTING = 1,
	SSH_SESSION_INIT = 2,
	SSH_SESSION_STARTUP = 3,
	SSH_KEEPALIVE_CONFIG = 4,
	SSH_USERAUTH_PUBLICKEY_FROMFILE = 5,
	SSH_SESSION_CONNECTED = 6
};

enum ssh_exec_state {
	SSH_CHANNEL_OPEN = 1,
	SSH_CHANNEL_EXEC = 2,
	SSH_CHANNEL_READ = 3,
	SSH_CHANNEL_SEND_EOF = 4,
	SSH_CHANNEL_WAIT_EOF = 5,
	SSH_CHANNEL_CLOSE = 6,
	SSH_CHANNEL_WAIT_CLOSED = 7,
	SSH_CHANNEL_FREE = 8
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

struct trans_ssh {
	int fd;
	enum ssh_state ssh_state;
	qb_loop_timer_handle keepalive_timer;
	LIBSSH2_SESSION *session;
	struct sockaddr_in sin;
	struct qb_list_head ssh_op_head;
	int scheduled;
};


int ssh_init_rc = -1;

static void transport_schedule(struct trans_ssh *trans_ssh)
{
	struct ssh_operation *ssh_op;

	if (qb_list_empty(&trans_ssh->ssh_op_head) == 0) {
		trans_ssh->scheduled = 1;
		ssh_op = qb_list_entry(trans_ssh->ssh_op_head.next, struct ssh_operation, list);
		qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
	} else {
		trans_ssh->scheduled = 0;
	}
}

static void transport_unschedule(struct trans_ssh *trans_ssh)
{
	struct ssh_operation *ssh_op;

	if (trans_ssh->scheduled) {
		trans_ssh->scheduled = 0;
		ssh_op = qb_list_entry(trans_ssh->ssh_op_head.next, struct ssh_operation, list);
		qb_loop_job_del(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
	}
}

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
	struct trans_ssh *trans_ssh = (struct trans_ssh *)ssh_op->assembly->transport;
	int rc;
	char buffer[4096];
	ssize_t rc_read;

	qb_enter();

	assert (trans_ssh->ssh_state == SSH_SESSION_CONNECTED);

	switch (ssh_op->ssh_exec_state) {
	case SSH_CHANNEL_OPEN:
		ssh_op->channel = libssh2_channel_open_session(trans_ssh->session);
		if (ssh_op->channel == NULL) {
			rc = libssh2_session_last_errno(trans_ssh->session);
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
		assert(0);
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
		assert(0);
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
		assert(0);
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
		assert(0);
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
				"libssh2_channel_read failed %"PRIu64, rc_read);
		assert(0);
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
		assert(0);
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
		assert(0);
		}
		break;

	default:
		assert(0);
	} /* switch */

	ssh_op_complete(ssh_op);

	transport_schedule(trans_ssh);

	qb_leave();

	return;

job_repeat_schedule:
	qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);

	qb_leave();
}

static void ssh_timeout(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	struct trans_ssh *trans_ssh = (struct trans_ssh *)ssh_op->assembly->transport;
	struct qb_list_head *list_temp;
	struct qb_list_head *list;
	struct ssh_operation *ssh_op_del;

	qb_enter();

	transport_unschedule(trans_ssh);

	qb_log(LOG_NOTICE, "ssh service timeout on assembly '%s'", ssh_op->assembly->name);
	qb_list_for_each_safe(list, list_temp, &trans_ssh->ssh_op_head) {
		ssh_op_del = qb_list_entry(list, struct ssh_operation, list);
		qb_loop_timer_del(NULL, ssh_op_del->ssh_timer);
		qb_list_del(list);
		qb_log(LOG_NOTICE, "delete ssh operation '%s'", ssh_op_del->command);
	}

	recover_state_set(&ssh_op->assembly->recover, RECOVER_STATE_FAILED);

	qb_leave();
}

static void ssh_keepalive_send(void *data)
{
	struct trans_ssh *trans_ssh = (struct trans_ssh *)data;
	int seconds_to_next;

	qb_enter();

	libssh2_keepalive_send(trans_ssh->session, &seconds_to_next);
	qb_loop_timer_add(NULL, QB_LOOP_LOW, seconds_to_next * 1000 * QB_TIME_NS_IN_MSEC,
		trans_ssh, ssh_keepalive_send, &trans_ssh->keepalive_timer);

	qb_leave();
}

static void ssh_assembly_connect(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	struct trans_ssh *trans_ssh = (struct trans_ssh *)assembly->transport;
	char name[PATH_MAX];
	char name_pub[PATH_MAX];
	int rc;

	qb_enter();

	switch (trans_ssh->ssh_state) {
	case SSH_SESSION_INIT:
		trans_ssh->session = libssh2_session_init();
		if (trans_ssh->session == NULL) {
			goto job_repeat_schedule;
		}

		libssh2_session_set_blocking(trans_ssh->session, 0);
		trans_ssh->ssh_state = SSH_SESSION_STARTUP;

		/*
                 * no break here is intentional
		 */

	case SSH_SESSION_STARTUP:
		rc = libssh2_session_startup(trans_ssh->session, trans_ssh->fd);
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
		trans_ssh->ssh_state = SSH_KEEPALIVE_CONFIG;

	case SSH_KEEPALIVE_CONFIG:
		libssh2_keepalive_config(trans_ssh->session, 1, KEEPALIVE_TIMEOUT);
		qb_loop_job_add(NULL, QB_LOOP_LOW, trans_ssh, ssh_keepalive_send);

		/*
                 * no break here is intentional
		 */
		trans_ssh->ssh_state = SSH_USERAUTH_PUBLICKEY_FROMFILE;

	case SSH_USERAUTH_PUBLICKEY_FROMFILE:
		snprintf (name, PATH_MAX, "/var/lib/pacemaker-cloud/keys/%s",
			assembly->name);
		snprintf (name_pub, PATH_MAX, "/var/lib/pacemaker-cloud/keys/%s.pub",
			assembly->name);
		rc = libssh2_userauth_publickey_fromfile(trans_ssh->session,
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

		recover_state_set(&assembly->recover, RECOVER_STATE_RUNNING);
		trans_ssh->ssh_state = SSH_SESSION_CONNECTED;

	case SSH_SESSION_CONNECTED:
		break;
	case SSH_SESSION_CONNECTING:
		assert(0);
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
	struct trans_ssh *trans_ssh = assembly->transport;

	qb_enter();
	/*
	 * Only execute an opperation when in the connected state
	 */
	if (trans_ssh->ssh_state != SSH_SESSION_CONNECTED) {
		qb_leave();
		return;
	}
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
	qb_list_add_tail(&ssh_op->list, &trans_ssh->ssh_op_head);

	if (trans_ssh->scheduled == 0) {
		transport_schedule(trans_ssh);
	}

	if (op) {
		pe_resource_ref(ssh_op->op);
	}

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
		transport_del(ssh_op->assembly->transport);
		ssh_op_delete(ssh_op);
		recover_state_set(&ssh_op->assembly->recover, RECOVER_STATE_FAILED);
		//free(ssh_op);
		qb_leave();
		return;
	}

	/*
	 * Add a healthcheck if asssembly is still running
	 */
	if (ssh_op->assembly->recover.state == RECOVER_STATE_RUNNING) {
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
	struct trans_ssh *trans_ssh = (struct trans_ssh *)assembly->transport;
	int rc;
	int flags;

	qb_enter();

	rc = connect(trans_ssh->fd, (struct sockaddr*)(&trans_ssh->sin),
		sizeof (struct sockaddr_in));
	if (rc == 0) {
		flags = fcntl(trans_ssh->fd, F_GETFL, 0);
		fcntl(trans_ssh->fd, F_SETFL, flags | (~O_NONBLOCK));
		qb_log(LOG_NOTICE, "Connected to assembly '%s'",
			assembly->name);
		trans_ssh->ssh_state = SSH_SESSION_INIT;
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, ssh_assembly_connect);
	} else {
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, connect_execute);
	}

	qb_leave();
}

void transport_del(void *ta)
{
	struct trans_ssh *trans_ssh = (struct trans_ssh *)ta;

	qb_enter();

	qb_loop_timer_del(NULL, trans_ssh->keepalive_timer);

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
transport_resource_action(struct assembly * a,
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
transport_connect(struct assembly * a)
{
	unsigned long hostaddr;
        struct trans_ssh *trans_ssh;
	int flags;

	qb_enter();

	if (ssh_init_rc != 0) {
		ssh_init_rc = libssh2_init(0);
	}
	assert(ssh_init_rc == 0);

	trans_ssh = calloc(1, sizeof(struct trans_ssh));
	a->transport = trans_ssh;

	hostaddr = inet_addr(a->address);
	trans_ssh->fd = socket(AF_INET, SOCK_STREAM, 0);
	flags = fcntl(trans_ssh->fd, F_GETFL, 0);
	fcntl(trans_ssh->fd, F_SETFL, flags | O_NONBLOCK);
	trans_ssh->sin.sin_family = AF_INET;
	trans_ssh->sin.sin_port = htons(22);
	trans_ssh->sin.sin_addr.s_addr = hostaddr;
	trans_ssh->ssh_state = SSH_SESSION_CONNECTING;
	qb_list_init(&trans_ssh->ssh_op_head);

	qb_log(LOG_NOTICE, "Connection in progress to assembly '%s'",
		a->name);

	qb_loop_job_add(NULL, QB_LOOP_LOW, a, connect_execute);

	qb_leave();
	return trans_ssh;
}

void transport_disconnect(struct assembly *a)
{
	struct trans_ssh *trans_ssh = (struct trans_ssh *)a->transport;
	struct qb_list_head *list_temp;
	struct qb_list_head *list;
	struct ssh_operation *ssh_op_del;

	qb_enter();

	qb_loop_timer_del(NULL, a->healthcheck_timer);

	/*
	 * Delete a transport connection in progress
	 */
	if (trans_ssh->ssh_state == SSH_SESSION_INIT) {
		qb_loop_job_del(NULL, QB_LOOP_LOW, a, connect_execute);
	}

	/*
	 * Delete a transport attempting an SSH connection
	 */
	if (trans_ssh->ssh_state != SSH_SESSION_CONNECTED) {
		qb_loop_job_del(NULL, QB_LOOP_LOW, a, ssh_assembly_connect);
	}

	if (trans_ssh->ssh_state == SSH_SESSION_CONNECTED) {
		transport_unschedule(trans_ssh);
	}

	/*
	 * Delete any outstanding ssh operations
	 */
	qb_list_for_each_safe(list, list_temp, &trans_ssh->ssh_op_head) {
		ssh_op_del = qb_list_entry(list, struct ssh_operation, list);

		qb_log(LOG_NOTICE, "delete ssh operation '%s'", ssh_op_del->command);

		qb_loop_timer_del(NULL, ssh_op_del->ssh_timer);
		qb_list_del(list);
		libssh2_channel_free(ssh_op_del->channel);
		free(ssh_op_del);
	}
	/*
	 * Free the SSH session associated with this transport
	 * there are no breaks intentionally in this switch
	 */
	switch (trans_ssh->ssh_state) {
	case SSH_SESSION_CONNECTED:
	case SSH_SESSION_STARTUP:
		libssh2_session_free(trans_ssh->session);
	case SSH_USERAUTH_PUBLICKEY_FROMFILE:
	case SSH_KEEPALIVE_CONFIG:
		qb_loop_timer_del(NULL, trans_ssh->keepalive_timer);
	case SSH_SESSION_INIT:
		qb_loop_job_del(NULL, QB_LOOP_LOW, a, ssh_assembly_connect);
	default:
		break;
	}

	free(a->transport);
	close(trans_ssh->fd);
	qb_leave();
}
