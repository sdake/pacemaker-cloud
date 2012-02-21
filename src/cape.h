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


#ifdef __cplusplus
extern "C" {
#endif

#include "pcmk_pe.h"

#define KEEPALIVE_TIMEOUT 15		/* seconds */
#define SSH_TIMEOUT 5000		/* milliseconds */
#define SCHEDULE_PROCESS_TIMEOUT 1000	/* milliseconds */
#define PENDING_TIMEOUT 100		/* milliseconds */
#define HEALTHCHECK_TIMEOUT 3000	/* milliseconds */


enum node_state {
	NODE_STATE_OFFLINE = 1,
	NODE_STATE_PENDING = 2,
	NODE_STATE_RUNNING = 3,
	NODE_STATE_FAILED = 4,
	NODE_STATE_RECOVERING = 5,
	NODE_STATE_ESCALATING = 6
};
#define NODE_NUM_STATES 5

typedef void (*repair_restart_fn_t)(void* inst);
typedef void (*repair_escalate_fn_t)(void* inst);

struct repair {
	void * instance;
	int escalating;
	int num_failures;
	int failure_period;
	qb_util_stopwatch_t *sw;
	repair_restart_fn_t restart;
	repair_escalate_fn_t escalate;
};
void repair_init(struct repair* r,
		 const char * escalation_failures,
		 const char * escalation_period,
		 repair_restart_fn_t repair_restart_fn,
		 repair_escalate_fn_t repair_escalate_fn);
void repair(struct repair* r);


struct assembly {
	char *name;
	char *uuid;
	char *address;
	char *instance_id;
	enum node_state instance_state;
	qb_map_t *resource_map;
	int fd;
	qb_loop_timer_handle healthcheck_timer;
	void *transport_assembly;
	qb_util_stopwatch_t *sw_instance_create;
	qb_util_stopwatch_t *sw_instance_connected;
};

struct resource {
	char *name;
	char *type;
	char *rclass;
	struct assembly *assembly;
	qb_loop_timer_handle monitor_timer;
	struct repair repair;
};

void resource_action_completed(struct pe_operation *op, enum ocf_exitcode rc);

void node_state_changed(struct assembly *assembly, enum node_state state);

void cape_init(void);

int cape_load(const char * name);

void cape_load_from_buffer(const char *buffer);

int32_t instance_create(struct assembly *assembly);

void instance_state_detect(void *data);

int instance_stop(struct assembly *a);

#ifdef __cplusplus
}
#endif
#endif /* CAPE_H_DEFINED */
