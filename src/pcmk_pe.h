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

#ifndef _PCMK_PE_H_
#define _PCMK_PE_H_

#include <qb/qbdefs.h>
#include <qb/qbmap.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <glib.h>
#include <libxml/parser.h>
#include <qb/qbutil.h>

#define PE_CRM_VERSION "3.0.5"
#define PE_DEFAULT_TIMEOUT 10000

enum ocf_exitcode {
	OCF_PENDING = -1,
	OCF_OK = 0,
	OCF_UNKNOWN_ERROR = 1,
	OCF_INVALID_PARAM = 2,
	OCF_UNIMPLEMENT_FEATURE = 3,
	OCF_INSUFFICIENT_PRIV = 4,
	OCF_NOT_INSTALLED = 5,
	OCF_NOT_CONFIGURED = 6,
	OCF_NOT_RUNNING = 7,
	OCF_RUNNING_MASTER = 8,
	OCF_FAILED_MASTER = 9,
};

struct pe_operation {
	char *hostname;
	char *node_uuid;
	char *method;
	char *rname;
	char *rclass;
	char *rprovider;
	char *rtype;
	qb_map_t *params;
	char *op_digest;
	uint32_t timeout;
	uint32_t times_executed;
	uint32_t interval;
	uint32_t target_outcome;
	void *user_data;
	void *graph;
	void *action;
	void *resource;
	uint32_t graph_id;
	uint32_t action_id;
	uint32_t refcount;
	qb_util_stopwatch_t *time_execed;
};

typedef void (*pe_resource_execute_t)(struct pe_operation *op);
typedef void (*pe_transition_completed_t)(void* user_data, int32_t result);

enum ocf_exitcode pe_resource_ocf_exitcode_get(struct pe_operation *op,
					       int lsb_exitcode);
const char* pe_resource_reason_get(enum ocf_exitcode exitcode);
int pe_resource_is_hard_error(enum ocf_exitcode ec);

void pe_resource_completed(struct pe_operation *op, uint32_t return_code);
void pe_resource_ref(struct pe_operation *op);
void pe_resource_unref(struct pe_operation *op);

int32_t pe_process_state(xmlDocPtr doc,
			 pe_resource_execute_t exec_fn,
			 pe_transition_completed_t done_fn,
			 void *user_data,
			 int debug);

int32_t pe_is_busy_processing(void);

void pe_log_init(int log_tag, int loglevel);

#ifdef __cplusplus
}
#endif

#endif /* _PCMK_PE_H_ */
