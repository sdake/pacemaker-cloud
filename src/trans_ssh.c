#include "pcmk_pe.h"

#include <sys/epoll.h>
#include <glib.h>
#include <uuid/uuid.h>
#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <qb/qbmap.h>
#include <libxml/parser.h>
#include <libxslt/transform.h>
#include <libdeltacloud/libdeltacloud.h>
#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <cape.h>

#define KEEPALIVE_TIMEOUT 15		/* seconds */
#define SSH_TIMEOUT 5000		/* milliseconds */
#define SCHEDULE_PROCESS_TIMEOUT 1000	/* milliseconds */
#define PENDING_TIMEOUT 1000		/* milliseconds */
#define HEALTHCHECK_TIMEOUT 3000	/* milliseconds */

static void assembly_ssh_exec(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	int rc;
	int rc_close;
	char *errmsg;
	int size;
	char buffer[4096];

	switch (ssh_op->ssh_exec_state) {
	case SSH_CHANNEL_OPEN:
		ssh_op->channel = libssh2_channel_open_session(ssh_op->assembly->session);
		if (ssh_op->channel == NULL) {
			rc = libssh2_session_last_errno(ssh_op->assembly->session);
			if (rc == LIBSSH2_ERROR_EAGAIN) {
				goto job_repeat_schedule;
			}
			qb_log(LOG_NOTICE,
				"open session failed %d\n", rc);
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
			goto error_close;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_READ;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_READ:
		rc = libssh2_channel_read(ssh_op->channel, buffer, sizeof(buffer));
		if (rc == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc < 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_read failed %d\n", rc);
			goto error_close;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_CLOSE;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_CLOSE:
		rc_close = libssh2_channel_close(ssh_op->channel);
		if (rc_close == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc_close != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel close failed %d\n", rc_close);
			return;
		}
		ssh_op->ssh_exec_state = SSH_CHANNEL_WAIT_CLOSED;

		/*
                 * no break here is intentional
		 */

	case SSH_CHANNEL_WAIT_CLOSED:
		rc_close = libssh2_channel_wait_closed(ssh_op->channel);
		if (rc_close == LIBSSH2_ERROR_EAGAIN) {
			goto job_repeat_schedule;
		}
		if (rc_close != 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_wait_closed failed %d\n", rc_close);
			return;
		}
		ssh_op->ssh_rc = libssh2_channel_get_exit_status(ssh_op->channel);
		ssh_op->ssh_exec_state = SSH_CHANNEL_FREE;

		/*
                 * no break here is intentional
		 */

error_close:
	case SSH_CHANNEL_FREE:
		rc = libssh2_channel_free(ssh_op->channel);
		if (rc < 0) {
			qb_log(LOG_NOTICE,
				"libssh2_channel_free failed %d\n", rc);
		}
		/*
                 * no break here is intentional
		 */
	} /* switch */

	qb_loop_timer_del(NULL, ssh_op->ssh_timer);
	qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, ssh_op->completion_func);
	qb_list_del(&ssh_op->list);
	return;

job_repeat_schedule:
	qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
}

static void ssh_timeout(void *data)
{
	struct ssh_operation *ssh_op = (struct ssh_operation *)data;
	struct qb_list_head *list_temp;
	struct qb_list_head *list;
	struct ssh_operation *ssh_op_del;

	qb_log(LOG_NOTICE, "ssh service timeout on assembly '%s'", ssh_op->assembly->name);
	qb_list_for_each_safe(list, list_temp, &ssh_op->assembly->ssh_op_head) {
		ssh_op_del = qb_list_entry(list, struct ssh_operation, list);
		qb_loop_timer_del(NULL, ssh_op_del->ssh_timer);
		qb_loop_job_del(NULL, QB_LOOP_LOW, ssh_op_del,
			assembly_ssh_exec);
		if (ssh_op_del->resource) {
			qb_loop_timer_del(NULL, ssh_op_del->resource->monitor_timer);
		}
		qb_list_del(list);
		qb_log(LOG_NOTICE, "delete ssh operation '%s'", ssh_op_del->command);
	}

	assembly_state_changed(ssh_op->assembly, INSTANCE_STATE_FAILED);
}

static void ssh_keepalive_send(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	int seconds_to_next;
	int rc;

	rc = libssh2_keepalive_send(assembly->session, &seconds_to_next);
	qb_loop_timer_add(NULL, QB_LOOP_LOW, seconds_to_next * 1000 * QB_TIME_NS_IN_MSEC,
		assembly, ssh_keepalive_send, &assembly->keepalive_timer);
}

static void ssh_assembly_connect(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	char name[1024];
	char name_pub[1024];
	int i;
	int rc;

	switch (assembly->ssh_state) {
	case SSH_SESSION_INIT:
		assembly->session = libssh2_session_init();
		if (assembly->session == NULL) {
			rc = libssh2_session_last_errno(assembly->session);
			if (rc == LIBSSH2_ERROR_EAGAIN) {
				goto job_repeat_schedule;
			}
			qb_log(LOG_NOTICE,
				"session init failed %d\n", rc);
		}

		libssh2_session_set_blocking(assembly->session, 0);
		assembly->ssh_state = SSH_SESSION_STARTUP;

		/*
                 * no break here is intentional
		 */

	case SSH_SESSION_STARTUP:
		rc = libssh2_session_startup(assembly->session, assembly->fd);
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
		assembly->ssh_state = SSH_KEEPALIVE_CONFIG;

	case SSH_KEEPALIVE_CONFIG:
		libssh2_keepalive_config(assembly->session, 1, KEEPALIVE_TIMEOUT);
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, ssh_keepalive_send);

		/*
                 * no break here is intentional
		 */
		assembly->ssh_state = SSH_USERAUTH_PUBLICKEY_FROMFILE;

	case SSH_USERAUTH_PUBLICKEY_FROMFILE:
		sprintf (name, "/var/lib/pacemaker-cloud/keys/%s",
			assembly->name);
		sprintf (name_pub, "/var/lib/pacemaker-cloud/keys/%s.pub",
			assembly->name);
		rc = libssh2_userauth_publickey_fromfile(assembly->session,
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

		assembly_state_changed(assembly, INSTANCE_STATE_RUNNING);
		assembly->ssh_state = SSH_CONNECTED;
	}
error:
	return;

job_repeat_schedule:
	qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, ssh_assembly_connect);
}

static void connect_execute(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	int rc;

	rc = connect(assembly->fd, (struct sockaddr*)(&assembly->sin),
		sizeof (struct sockaddr_in));
	if (rc == 0) {
		qb_log(LOG_NOTICE, "Connected to assembly '%s'",
			assembly->name);
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, ssh_assembly_connect);
	} else {
		qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, connect_execute);
	}
}

void assembly_connect(struct assembly *assembly)
{
	unsigned long hostaddr;
	int rc;

	hostaddr = inet_addr(assembly->address);
	assembly->fd = socket(AF_INET, SOCK_STREAM, 0);
	assembly->sin.sin_family = AF_INET;
	assembly->sin.sin_port = htons(22);
	assembly->sin.sin_addr.s_addr = hostaddr;
	assembly->ssh_state = SSH_SESSION_INIT;

	qb_log(LOG_NOTICE, "Connection in progress to assembly '%s'",
		assembly->name);

	qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, connect_execute);
}

void ssh_nonblocking_exec(struct assembly *assembly,
	struct resource *resource, struct pe_operation *op,
	void (*completion_func)(void *),
	char *format, ...)
{
	va_list ap;
	struct ssh_operation *ssh_op;

	ssh_op = calloc(1, sizeof(struct ssh_operation));

	va_start(ap, format);
	vsprintf(ssh_op->command, format, ap);
	va_end(ap);

	qb_log(LOG_NOTICE, "ssh_exec for assembly '%s' command '%s'",
		assembly->name, ssh_op->command);

	ssh_op->assembly = assembly;
	ssh_op->ssh_rc = 0;
	ssh_op->op = op;
	ssh_op->ssh_exec_state = SSH_CHANNEL_OPEN;
	ssh_op->resource = resource;
	ssh_op->completion_func = completion_func;
	qb_list_init(&ssh_op->list);
	qb_list_add_tail(&ssh_op->list, &ssh_op->assembly->ssh_op_head);
	qb_loop_job_add(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
	qb_loop_timer_add(NULL, QB_LOOP_LOW,
		SSH_TIMEOUT * QB_TIME_NS_IN_MSEC,
		ssh_op, ssh_timeout, &ssh_op->ssh_timer);
}


ssh_op_delete(struct ssh_operation *ssh_op)
{
	qb_loop_job_del(NULL, QB_LOOP_LOW, ssh_op, assembly_ssh_exec);
}

