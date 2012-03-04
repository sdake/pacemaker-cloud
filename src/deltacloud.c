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

#include <qb/qblog.h>
#include <libdeltacloud/libdeltacloud.h>

#include "config.h"

#include "cape.h"
#include "trans.h"

/*
 * External API
 */
void instance_state_get(char *instance_id,
	void (*completion_func)(char *status, char *ip_addr, void *data),
	void *data)
{
	struct deltacloud_api api;
	struct deltacloud_instance instance;
	int rc;

	rc = deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "");
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}

	rc = deltacloud_get_instance_by_id(&api, instance_id, &instance);
	if (strcmp(instance.state, "RUNNING") == 0 && instance.private_addresses->address) {
		completion_func("ACTIVE", instance.private_addresses->address, data);
	} else {
		completion_func("PENDING", NULL, data);
	}
}

void instance_create_from_image_id(char *image_id,
	void (*completion_func)(char *instance_id, void *data),
	void *data)
{
	struct deltacloud_api api;
	char *instance_id;
	int rc;

	rc = deltacloud_initialize(&api, "http://localhost:3001/api", "dep-wp", "");
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}
	rc = deltacloud_create_instance(&api, image_id, NULL, 0, &instance_id);
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}
	completion_func(instance_id, data);
        deltacloud_free(&api);
}


void image_id_get(char *image_name,
	void (*completion_func)(char *image_id, void *data),
	void *data)
{
	static struct deltacloud_api api;
	struct deltacloud_image *images_head;
	struct deltacloud_image *images;
	int rc;

	qb_enter();

	if (deltacloud_initialize(&api, "http://localhost:3001/api",
				  "dep-wp", "") < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}
	rc = deltacloud_get_images(&api, &images);
	if (rc < 0) {
		qb_log(LOG_ERR, "Failed to initialize libdeltacloud: %s",
		       deltacloud_get_last_error_string());

		qb_leave();
		return;
	}

	for (images_head = images; images; images = images->next) {
		if (strcmp(images->name, image_name) == 0) {
			completion_func(images->id, data);
			break;
		}
	}
	deltacloud_free_image_list(&images_head);
	deltacloud_free(&api);

	qb_leave();
}
