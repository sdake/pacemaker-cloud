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
#ifndef _CONFIG_LOADER_H_
#define _CONFIG_LOADER_H_

#include <string>
#include <libxml/parser.h>
#include <libxml/tree.h>

/**
 * get the list of deployable to be managed by cpe
 *
 * @return return code
 */
int32_t
config_list();

/**
 * get the XML config for a given deployable uuid
 *
 * @param uuid(in) deployable uuid
 * @param xml(out) xml object
 * @return return code
 */
int32_t
config_get(std::string& uuid, xmlDoc** doc);

#endif /* _CONFIG_LOADER_H_ */

