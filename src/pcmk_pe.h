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

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <glib.h>
#include <libxml/parser.h>


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
	char *method;
	char *rname;
	char *rclass;
	char *rprovider;
	char *rtype;
	GHashTable *params;
	uint32_t timeout;
	uint32_t interval;
	void *user_data;
};

enum ocf_exitcode pe_get_ocf_exitcode(const char *action, int lsb_exitcode);

typedef uint32_t (*pe_resource_execute_t)(struct pe_operation *op);

int32_t pe_process_state(xmlNode *xml_input, pe_resource_execute_t fn,
			 void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* _PCMK_PE_H_ */
