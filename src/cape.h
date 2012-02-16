#include <libssh2.h>

#define KEEPALIVE_TIMEOUT 15		/* seconds */
#define SSH_TIMEOUT 5000		/* milliseconds */
#define SCHEDULE_PROCESS_TIMEOUT 1000	/* milliseconds */
#define PENDING_TIMEOUT 1000		/* milliseconds */
#define HEALTHCHECK_TIMEOUT 3000	/* milliseconds */


enum instance_state {
	INSTANCE_STATE_OFFLINE = 1,
	INSTANCE_STATE_PENDING = 2,
	INSTANCE_STATE_RUNNING = 3,
	INSTANCE_STATE_FAILED = 4,
	INSTANCE_STATE_RECOVERING = 5
};

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
	SSH_CHANNEL_CLOSE = 3,
	SSH_CHANNEL_WAIT_CLOSED = 4,
	SSH_CHANNEL_FREE = 5
};

struct assembly {
	char *name;
	char *uuid;
	char *address;
	char *instance_id;
	enum instance_state instance_state;
	struct qb_list_head ssh_op_head;
	enum ssh_state ssh_state;
	qb_map_t *resource_map;
	int fd;
	qb_loop_timer_handle healthcheck_timer;
	qb_loop_timer_handle keepalive_timer;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	struct sockaddr_in sin;
};

struct resource {
	char *name;
	char *type;
	char *class;
	struct assembly *assembly;
	qb_loop_timer_handle monitor_timer;
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
	char command[4096];
};
