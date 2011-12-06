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
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <qb/qbdefs.h>
#include <qb/qbmap.h>
#include <qb/qblog.h>

#include <libdeltacloud/libdeltacloud.h>

enum state {
	STATE_INIT,
	STATE_OFFLINE,
	STATE_ONLINE,
};
#define NUM_STATES 3;

struct assembly {
	enum state state;
	char *name;
	char *uuid;
	uint32_t inst_id;
	uint32_t image_id;
};

typedef uint32_t (*fsm_state_fn)(void);
typedef void (*fsm_action_fn)(void);

static fsm_state_fn state_table[NUM_STATES] = {
	NULL,
	check_state_offline,
	check_state_online
};

static fsm_action_fn state_action_table[NUM_STATES][NUM_STATES] = {
	{NULL, NULL, NULL},
	{NULL, NULL, state_offline_to_online},
	{NULL, state_online_to_offline, NULL}
};



static uint32_t
check_state_online(void)
{
	uint32_t new_state = _state;
	gdouble elapsed = 0;

	if (_hb_state == HEARTBEAT_OK) {
		elapsed = g_timer_elapsed(_last_heartbeat, NULL);
		if (elapsed > (5 * 1.5)) {
			_hb_state = Assembly::HEARTBEAT_NOT_RECEIVED;
			qb_log(LOG_WARNING,
			       "assembly (%s) heartbeat too late! (%.2f > 5 seconds)",
			       _name.c_str(), elapsed);
		}
	}
	if (_hb_state != HEARTBEAT_OK) {
		new_state = STATE_OFFLINE;
	}
	return new_state;
}

static uint32_t
check_state_offline(void)
{
	uint32_t new_state = _state;
	if (_hb_state == HEARTBEAT_OK &&
	    _mh_rsc.is_connected() &&
	    _mh_serv.is_connected() &&
	    _mh_host.is_connected()) {
		new_state = STATE_ONLINE;
	}
	return new_state;
}

static void
state_offline_to_online(void)
{
	qb_loop_timer_handle th;

	qb_log(LOG_NOTICE, "Assembly (%s) STATE_ONLINE.",
	       _name.c_str());
	_dep->assembly_state_changed(this, "running", "All good");

	mainloop_timer_add(4000, this,
			   heartbeat_check_tmo, &th);
}

static void
state_online_to_offline(void)
{

	qb_log(LOG_NOTICE, "Assembly (%s) STATE_OFFLINE.",
	       _name.c_str());
	_dep->assembly_state_changed(this, "failed", "Not reachable");
}


struct assembly*
ass_create(qb_loop_t *l, char *name, char *uuid)
{
	struct assembly* a = calloc(1, sizeof(struct assembly));

	a->uuid = strdup(uuid);
	a->name = strdup(name);
	a->state = STATE_INIT;

	return a;
}

static int
start_deployable(struct deltacloud_api *api, char *dep_name)
{
	struct deltacloud_instance *instances = NULL;
	struct deltacloud_instance *n = NULL;

        struct deltacloud_image *images = NULL;
        struct deltacloud_image *m = NULL;

	if (deltacloud_get_images(&api, &images) < 0) {
		qb_log(LOG_ERR, "Failed to get deltacloud images: %s\n",
			deltacloud_get_last_error_string());
		goto cleanup;
	}

	printf("=== images === \n");
	m = images;
	do {
		printf("%s [id:%s] - %s\n",
		       m->name, m->id, m->state);
		m = m->next;
	} while (m);


	if (deltacloud_get_instances(&api, &instances) < 0) {
		qb_log(LOG_ERR, "Failed to get deltacloud instances: %s\n",
			deltacloud_get_last_error_string());
		goto cleanup;
	}

	printf("=== instances === \n");
	n = instances;
	do {
		printf("inst_id: %s (%s) image_id:%s\n",
		       n->id, n->state, n->image_id);
		n = n->next;
	} while (n);


	deltacloud_free_instance_list(&instances);
}
