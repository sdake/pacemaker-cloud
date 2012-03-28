/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Zane Bitter <zbitter@redhat.com>
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

#include "trans.h"
#include "cim_service.h"

#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>


typedef struct {
    void *transport;
    const char *service;
    int (*action_func)(void *, const char *);
    struct pe_operation *op;
} cim_action_t;


void *
transport_connect(struct assembly *a)
{
    qb_enter();

    assert(a != NULL);

    if (!a->transport) {
        a->transport = cim_service_connect(a->address);
    }

    qb_leave();

    return a->transport;
}

void
transport_disconnect(struct assembly *a)
{
    qb_enter();

    assert(a != NULL);
    cim_service_disconnect(a->transport);

    qb_leave();
}

static void
perform_action(void *data)
{
    enum ocf_exitcode result;
    cim_action_t *action = data;

    qb_enter();

    assert(action != NULL);
    assert(action->action_func != NULL);

    result = action->action_func(action->transport, action->service);

    resource_action_completed(action->op, result);

    pe_resource_unref(action->op);
    free(data);

    qb_leave();
}

void
transport_resource_action(struct assembly *a,
                          struct resource *resource,
                          struct pe_operation *op)
{
    int32_t result = -1;
    cim_action_t *action;

    qb_enter();

    assert(a != NULL);
    assert(op != NULL);

    pe_resource_ref(op);

    action = malloc(sizeof(*action));
    if (!action) {
        qb_log(LOG_ERR, "Failed to allocate resource action");
        goto done;
    }

    action->transport = a->transport;
    action->op = op;
    action->service = op->rtype;

    // TODO handle OCF resources
    assert(strcmp(op->rclass, "lsb") == 0);

    if (strcmp(op->method, "monitor") == 0) {
        action->action_func = cim_service_started;
    } else {
        if (strcmp(op->method, "start") == 0) {
            action->action_func = cim_service_start;
        } else if (strcmp(op->method, "stop") == 0) {
            action->action_func = cim_service_stop;
        } else {
            assert(0);
            goto done;
        }
    }

    result = qb_loop_job_add(NULL, QB_LOOP_LOW, action, &perform_action);

done:
    if (result) {
        recover_state_set(&a->recover, RECOVER_STATE_FAILED);
        pe_resource_unref(action->op);
        free(action);
    }
    qb_leave();
}

