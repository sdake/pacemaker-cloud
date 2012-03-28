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

#ifndef CIM_SERVICE_H_DEFINED
#define CIM_SERVICE_H_DEFINED

/**
 * Open a connection to the specified CIM server.
 * @param server the server to connect to
 * @return a handle to a CIM connection.
 */
void *cim_service_connect(const char *server);

/**
 * Close the connection to a CIM server.
 * @param transport handle to the connection
 */
void cim_service_disconnect(void *transport);

/**
 * Query the specified service to see if it is running.
 * @param transport handle to the connection
 * @param service the name of the service to query
 * @return 1 if the service is started, 0 if it is not started, negative if
 * an error occurred.
 */
int cim_service_started(void *transport, const char *service);

/**
 * Start the specified service.
 * @param transport handle to the connection
 * @param service the name of the service to start
 * @return 0 on success, negative on failure.
 */
int cim_service_start(void *transport, const char *service);

/**
 * Stop the specified service.
 * @param transport handle to the connection
 * @param service the name of the service to stop
 * @return 0 on success, negative on failure.
 */
int cim_service_stop(void *transport, const char *service);

#endif /* CIM_SERVICE_H_DEFINED */
