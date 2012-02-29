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

#ifndef CAPE_H_DEFINED
#define CAPE_H_DEFINED

#include <libssh2.h>

#include <qb/qblist.h>
#include <qb/qbmap.h>
#include <qb/qbutil.h>
#include <qb/qbloop.h>


#ifdef __cplusplus
extern "C" {
#endif

#include "pcmk_pe.h"

/*
 * Limits of the system
 */
#define ASSEMBLY_NAME_MAX 1024		/* Maximum assembly length in bytes */
#define RESOURCE_NAME_MAX 1024		/* Maximum resource length in bytes */
#define METHOD_NAME_MAX 20		/* Maximum method name in bytes */
#define OP_NAME_MAX 15			/* Maximum interval length in bytes */

/*
 * Timers of the system
 */
#define KEEPALIVE_TIMEOUT 15		/* seconds */
#define SSH_TIMEOUT 5000		/* milliseconds */
#define PENDING_TIMEOUT 100		/* milliseconds */
#define HEALTHCHECK_TIMEOUT 3000	/* milliseconds */

struct application {
	char *name;
	char *uuid;
	qb_map_t *node_map;
};

enum recover_state {
	RECOVER_STATE_UNKNOWN,
	RECOVER_STATE_RUNNING,
	RECOVER_STATE_FAILED,
	RECOVER_STATE_STOPPED,
	RECOVER_STATE_UNRECOVERABLE,
};
#define NODE_NUM_STATES 3 /* only the first 3 states used by matahari.cpp */

typedef void (*recover_restart_fn_t)(void* inst);
typedef void (*recover_escalate_fn_t)(void* inst);
typedef void (*recover_state_changing_fn_t)(void* inst,
					    enum recover_state from,
					    enum recover_state to);
struct recover {
	void * instance;
	enum recover_state state;
	int num_failures;
	int failure_period;
	qb_util_stopwatch_t *sw;
	recover_restart_fn_t restart;
	recover_escalate_fn_t escalate;
	recover_state_changing_fn_t state_changing;
};
void recover_init(struct recover* r,
		 const char * escalation_failures,
		 const char * escalation_period,
		 recover_restart_fn_t recover_restart_fn,
		 recover_escalate_fn_t recover_escalate_fn,
		 recover_state_changing_fn_t recover_state_changing_fn);

void recover_state_set(struct recover* r, enum recover_state state);

struct assembly {
	char *name;
	char *uuid;
	char *address;
	char *instance_id;
	struct application *application;
	qb_map_t *resource_map;
	int fd;
	void *transport;
	qb_util_stopwatch_t *sw_instance_create;
	qb_util_stopwatch_t *sw_instance_connected;
	struct recover recover;
};

struct resource {
	char *name;
	char *type;
	char *rclass;
	struct assembly *assembly;
	struct pe_operation *monitor_op;
	qb_loop_timer_handle monitor_timer;
	struct recover recover;
};

void resource_action_completed(struct pe_operation *op, enum ocf_exitcode rc);

void cape_init(int debug);

int cape_load(const char * name);

void cape_load_from_buffer(const char *buffer);

int32_t cape_admin_init(void);

void cape_admin_event_send(const char *app,
			   struct assembly *a,
			   struct resource *r,
			   const char *state,
			   const char *reason);
void cape_admin_fini(void);

int32_t instance_create(struct assembly *assembly);

int instance_stop(struct assembly *a);

void cape_exit(void);

#ifdef __cplusplus
}
#endif
#endif /* CAPE_H_DEFINED */
