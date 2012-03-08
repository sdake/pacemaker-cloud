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
#include <check.h>
#include <stdlib.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>

#include "cape.h"
#include "trans.h"

#define FAILURE_PERCENT 90

void transport_resource_action(struct assembly *a,
        struct resource *resource,
        struct pe_operation *op)
{
	qb_enter();

	if (strcmp(op->method, "monitor") == 0 && op->interval > 0) {
	if ((random() % 100) < FAILURE_PERCENT) {
		resource_action_completed(op, OCF_NOT_RUNNING);
	} else {
		resource_action_completed(op, op->target_outcome);
	}
	} else {
	resource_action_completed(op, op->target_outcome);
	}

	qb_leave();

	return;
}

void *transport_connect(struct assembly *a)
{
	qb_enter();

	qb_leave();

	return NULL;
}

void transport_disconnect(struct assembly *a)
{
	qb_enter();

	qb_leave();
}

void transport_del(void *transport);

int32_t instance_create(struct assembly *assembly)
{
	qb_enter();

	recover_state_set(&assembly->recover, RECOVER_STATE_UNKNOWN);

	recover_state_set(&assembly->recover, RECOVER_STATE_RUNNING);

	qb_leave();
	return 0;
}

int instance_destroy(struct assembly *a)
{
	qb_enter();
	qb_leave();

	return 0;
}

