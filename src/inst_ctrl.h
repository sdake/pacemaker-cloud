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

#ifndef INST_CTRL_H_DEFINED
#define INST_CTRL_H_DEFINED

void instance_state_get(char *instance_id,
	void (*completion_func)(char *status, char *ip_addr, void *data),
	void *data);

void image_id_get(char *image_name,
	void (*completion_func)(char *image_id, void *data),
	void *data);

void instance_create_from_image_id(char *image_id,
	void (*completion_func)(char *instance_id, void *data),
	void *data);

#endif /* INST_CTRL_H_DEFINED */
