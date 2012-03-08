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

#include <stdio.h>
#include <glib.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <assert.h>
#include <libdeltacloud/libdeltacloud.h>

#include "config.h"

#include "cape.h"
#include "trans.h"

static qb_loop_timer_handle timer_handle;

/*
 * Internal Implementation
 */
static void instance_state_detect(void *data)
{
	static struct deltacloud_api api;
	struct assembly *assembly = (struct assembly *)data;
	struct deltacloud_instance instance;
	int rc;

	qb_enter();

	if (deltacloud_initialize(&api, "http://localhost:3001/api",
				  assembly->application->name, "") < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}

	rc = deltacloud_get_instance_by_id(&api, assembly->instance_id, &instance);
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}


	if (strcmp(instance.state, "RUNNING") == 0) {
		assembly->address = strdup (instance.private_addresses->address);
		qb_util_stopwatch_stop(assembly->sw_instance_create);
		qb_log(LOG_INFO, "Instance '%s' with address '%s' changed to RUNNING in (%lld ms).",
			assembly->name, assembly->address,
			qb_util_stopwatch_us_elapsed_get(assembly->sw_instance_create) / 1000);
		qb_util_stopwatch_start(assembly->sw_instance_connected);
		transport_connect(assembly);
	} else
	if (strcmp(instance.state, "PENDING") == 0) {
		recover_state_set(&assembly->recover, RECOVER_STATE_UNKNOWN);
		qb_loop_timer_add(NULL, QB_LOOP_LOW,
			PENDING_TIMEOUT * QB_TIME_NS_IN_MSEC, assembly,
			instance_state_detect, &timer_handle);
	}

	deltacloud_free_instance(&instance);
	deltacloud_free(&api);
	qb_leave();
}

/*
 * External API
 */
int32_t instance_create(struct assembly *assembly)
{
	static struct deltacloud_api api;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;
	FILE *fp;

	qb_enter();

	qb_util_stopwatch_start(assembly->sw_instance_create);
	if (deltacloud_initialize(&api, "http://localhost:3001/api",
				  assembly->application->name, "") < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return -1;
	}
	rc = deltacloud_get_images(&api, &images);
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return -1;
	}

	for (images_head = images; images; images = images->next) {
		if (strcmp(images->name, assembly->name) == 0) {
			rc = deltacloud_create_instance(&api, images->id, NULL, 0, &assembly->instance_id);
			if (rc < 0) {
				fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
				deltacloud_get_last_error_string());
				return -1;
			}
			fp = fopen(assembly->name, "w+");
			fwrite (assembly->instance_id, strlen (assembly->instance_id), 1, fp);
			fclose(fp);
			instance_state_detect(assembly);
		}
	}
	deltacloud_free_image_list(&images_head);
	deltacloud_free(&api);

	qb_leave();
	return 0;
}

int instance_destroy(struct assembly *a)
{
	static struct deltacloud_api api;
	struct deltacloud_instance *instances = NULL;
	struct deltacloud_instance *instances_head = NULL;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;
	char *image_name = a->name;

	qb_enter();

	if (deltacloud_initialize(&api, "http://localhost:3001/api",
				  a->application->name, "") < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return -1;
	}
	if (deltacloud_get_instances(&api, &instances) < 0) {
	qb_log(LOG_ERR, "Failed to get deltacloud instances: %s",
		deltacloud_get_last_error_string());

		qb_leave();
		return -1;
	}

	rc = deltacloud_get_images(&api, &images);
	images_head = images;
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return -1;
	}

	instances_head = instances;
	for (; images; images = images->next) {
		for (instances = instances_head; instances; instances = instances->next) {
			if (strcmp(images->name, image_name) == 0) {
				if (strcmp(instances->image_id, images->id) == 0) {
					deltacloud_instance_stop(&api, instances);
					break;
				}
			}
		}
	}

	deltacloud_free_image_list(&images_head);
	deltacloud_free_instance_list(&instances_head);
	deltacloud_free(&api);

	qb_leave();

	return 0;
}

