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
#include <glib.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <assert.h>
#include <libdeltacloud/libdeltacloud.h>

#include "cape.h"
#include "trans.h"

static qb_loop_timer_handle timer_handle;

void instance_state_detect(void *data)
{
	static struct deltacloud_api api;
	struct assembly *assembly = (struct assembly *)data;
	struct deltacloud_instance instance;
	struct deltacloud_instance *instance_p;
	int rc;
	int i;
	char *sptr;
	char *sptr_end;

	if (deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "") < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return;
	}

	rc = deltacloud_get_instance_by_id(&api, assembly->instance_id, &instance);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return;
	}


	if (strcmp(instance.state, "RUNNING") == 0) {
		for (i = 0, instance_p = &instance; instance_p; instance_p = instance_p->next, i++) {
			/*
			 * Eliminate the garbage output of this DC api
			 */
			sptr = instance_p->private_addresses->address +
				strspn (instance_p->private_addresses->address, " \t\n");
			sptr_end = sptr + strcspn (sptr, " \t\n");
			*sptr_end = '\0';
		}

		assembly->address = strdup (sptr);
		qb_log(LOG_INFO, "Instance '%s' changed to RUNNING.",
			assembly->name);
		ta_connect(assembly);
	} else
	if (strcmp(instance.state, "PENDING") == 0) {
		qb_log(LOG_INFO, "Instance '%s' is PENDING.",
			assembly->name);
		qb_loop_timer_add(NULL, QB_LOOP_LOW,
			PENDING_TIMEOUT * QB_TIME_NS_IN_MSEC, assembly,
			instance_state_detect, &timer_handle);
	}
}

int32_t instance_create(struct assembly *assembly)
{
	static struct deltacloud_api api;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;

	if (deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "") < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}
	rc = deltacloud_get_images(&api, &images);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
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
			instance_state_detect(assembly);
		}
	}
	deltacloud_free_image_list(&images_head);
	deltacloud_free(&api);
	return 0;
}

int instance_stop(char *image_name)
{
	static struct deltacloud_api api;
	struct deltacloud_instance *instances = NULL;
	struct deltacloud_instance *instances_head = NULL;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;

	if (deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "") < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}
	if (deltacloud_get_instances(&api, &instances) < 0) {
	fprintf(stderr, "Failed to get deltacloud instances: %s\n",
		deltacloud_get_last_error_string());
		return -1;
	}

	rc = deltacloud_get_images(&api, &images);
	images_head = images;
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libdeltacloud: %s\n",
		deltacloud_get_last_error_string());
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
	return 0;
}

