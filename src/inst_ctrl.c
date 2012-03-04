/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Authors: Steven Dake <sdake@redhat.com>
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

#include <glib.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <assert.h>
#include <curl/curl.h>
#include <memory.h>
#include <libxml2/libxml/parser.h>
#include <qb/qbutil.h>

#include "config.h"

#include "cape.h"
#include "trans.h"
#include "inst_ctrl.h"

/*
 * Internal implementation
 */
static void instance_state_completion(char *state, char *address, void *data);

static void my_instance_state_get(void *data)
{
	struct assembly *assembly = (struct assembly *)data;
	instance_state_get(assembly->instance_id, instance_state_completion, data);
}

static void image_id_get_completion(char *image_id, void *data)
{
	struct assembly *assembly = (struct assembly *)data;

	strcpy(assembly->image_id, image_id);
}

static void instance_create_completion(char *instance_id, void *data)
{
	struct assembly *assembly = (struct assembly *)data;

	strcpy(assembly->instance_id, instance_id);
}

static void instance_state_completion(char *state, char *address, void *data)
{
	struct assembly *assembly = (struct assembly *)data;

	if (strcmp(state, "ACTIVE") == 0) {
		assembly->address = strdup(address);
		qb_util_stopwatch_stop(assembly->sw_instance_create);
		qb_log(LOG_INFO, "Instance '%s' with address '%s' changed to RUNNING in (%lld ms).",
			assembly->name, assembly->address,
			qb_util_stopwatch_us_elapsed_get(assembly->sw_instance_create) / 1000);
		qb_util_stopwatch_start(assembly->sw_instance_connected);
		transport_connect(assembly);
	} else {
		recover_state_set(&assembly->recover, RECOVER_STATE_UNKNOWN);
		qb_loop_timer_add(NULL, QB_LOOP_LOW,
			PENDING_TIMEOUT * QB_TIME_NS_IN_MSEC, assembly,
			my_instance_state_get, NULL);
	}
}

/*
 * External API
 */
int32_t instance_create(struct assembly *assembly)
{
	qb_enter();

	qb_util_stopwatch_start(assembly->sw_instance_create);
	image_id_get(assembly->name, image_id_get_completion, assembly);
	instance_create_from_image_id(assembly->image_id, instance_create_completion, assembly);
	qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, my_instance_state_get);

	qb_leave();
	return 0;
}

int instance_stop(struct assembly *a)
{
	return 0;
}
