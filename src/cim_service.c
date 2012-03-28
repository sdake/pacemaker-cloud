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

#include "cim_service.h"

#include <stdlib.h>
#include <assert.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>

#include <cimc/cimc.h>
#include <CimClientLib/cmci.h>
#include <CimClientLib/native.h>
#include <CimClientLib/cmcimacs.h>


static CIMCEnv *ce = NULL;


static void
log_cim_error(CIMCStatus *status, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    qb_log(LOG_ERR, "%s (CIM Error %u%s%s)", buffer,
           status->rc,
           status->msg ? " - " : "",
           status->msg ? CMGetCharsPtr(status->msg, NULL) : "");
}

void *
cim_service_connect(const char *server)
{
    CIMCClient *client;
    CIMCStatus status = {};

    qb_enter();

    if (!ce) {
        int rc;
        char *msg;

        ce = NewCIMCEnv("XML", 0, &rc, &msg);
        if (!ce) {
            qb_log(LOG_ERR, "Failed to create CIM env (%s)", msg);
            qb_leave();
            return NULL;
        }
    }

    client = ce->ft->connect(ce,
                             server, "http", "5988",
                             NULL, NULL, // TODO login credentials
                             &status);
    if (!client) {
        log_cim_error(&status, "CIM connection failed");
    }

    if (status.msg) {
        CMRelease(status.msg);
    }

    qb_leave();
    return client;
}

void
cim_service_disconnect(void *transport)
{
    CIMCClient *client = transport;

    qb_enter();

    assert(transport != NULL);

    if (client) {
        CMRelease(client);
    }

    qb_leave();
}

static CIMCObjectPath *
create_object_path(const char *service)
{
    CIMCObjectPath *path;
    CIMCValue service_name;
    CIMCStatus status = {};

    // TODO look up class name dynamically?
    path = ce->ft->newObjectPath(ce, "root/cimv2", "Linux_Service", &status);

    service_name.string = ce->ft->newString(ce, service, &status);
    if (status.rc) {
        log_cim_error(&status, "Error creating CIM String (%s)");
        return NULL;
    }
    path->ft->addKey(path, "Name", &service_name, CIMC_string);

    if (status.msg) {
        CMRelease(status.msg);
    }

    return path;
}

static int
service_get_property(CIMCClient *client, const char *service,
                     const char *property, CIMCData *result)
{
    CIMCObjectPath *path;
    CIMCStatus status = {};
    int rc = 0;

    path = create_object_path(service);

    if (path) {
        qb_log(LOG_DEBUG,
               "Querying CIM \"%s\" property for service \"%s\"",
               property, service);

        *result = client->ft->getProperty(client, path, property, &status);
        if (status.rc) {
            log_cim_error(&status, "Error getting service \"%s\" property",
                          service);
            rc = -1;
        }

        CMRelease(path);
    }

    if (status.msg) {
        CMRelease(status.msg);
    }
    return rc;
}

int
cim_service_started(void *transport, const char *service)
{
    int rc;
    CIMCData result;
    int started = -1;

    qb_enter();

    assert(transport != NULL);
    assert(service != NULL);

    rc = service_get_property(transport, service, "Started", &result);

    if (!rc) {
        if (result.type == CIMC_boolean) {
            started = result.value.boolean;

            qb_log(LOG_INFO, "Service \"%s\" is %s", service,
                   started ? "started" : "stopped");
        }
    }

    qb_leave();
    return started;
}

static int
service_set_enabled(CIMCClient *client, const char *service, int enabled)
{
    int rc = -1;
    CIMCObjectPath *path;
    CIMCStatus status = {};
    const char *method = enabled ? "StartService" : "StopService";

    path = create_object_path(service);

    if (path) {
        CIMCData result;

        qb_log(LOG_INFO, "Invoking CIM method %s for service \"%s\"",
               method, service);

        result = client->ft->invokeMethod(client, path, method,
                                          NULL, NULL, &status);
        if (status.rc) {
            log_cim_error(&status,
                          "Error invoking CIM Method \"%s\" on service \"%s\"",
                          method, service);
        } else {
            if (result.type == CIMC_uint32) {
                switch (result.value.uint32) {
                    case 0:
                        rc = 0;
                        break;
                    case 1:
                        qb_log(LOG_ERR, "Method \"%s\" not supported", method);
                        break;
                    default:
                        qb_log(LOG_ERR, "Method \"%s\" returned error %u",
                               method, result.value.uint32);
                        break;
                }
            }
        }

        CMRelease(path);
    }

    if (status.msg) {
        CMRelease(status.msg);
    }
    return rc;
}

int
cim_service_start(void *transport, const char *service)
{
    int result;

    qb_enter();

    assert(transport != NULL);
    assert(service != NULL);

    result = service_set_enabled(transport, service, 1);

    qb_leave();
    return result;
}

int
cim_service_stop(void *transport, const char *service)
{
    int result;

    qb_enter();

    assert(transport != NULL);
    assert(service != NULL);

    result = service_set_enabled(transport, service, 0);

    qb_leave();
    return result;
}

