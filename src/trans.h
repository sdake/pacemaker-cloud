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
 * GNU Lesser General Public License for more detransportils.
 *
 * You should have received a copy of the GNU General Public License
 * along with pacemaker-cloud.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TRANS_H_DEFINED
#define TRANS_H_DEFINED

#ifdef __cplusplus
extern "C" {
#endif
#include "pcmk_pe.h"
#include "cape.h"

void transport_resource_action(struct assembly *a,
	struct resource *resource,
	struct pe_operation *op);

void *transport_connect(struct assembly *a);

void transport_disconnect(struct assembly *a);

void transport_del(void *transport);

void
transport_execute(void *transport,
	void (*completion_func)(void *data, int rc),
        void (*timeout_func)(void *data),
        void *data,
        uint64_t timeout_msec,
        char *format, ...);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* TRANS_H_DEFINED */
