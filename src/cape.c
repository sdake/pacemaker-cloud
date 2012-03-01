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
#include "pcmk_pe.h"

#include <inttypes.h>
#include <glib.h>
#include <uuid/uuid.h>
#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <qb/qbmap.h>
#include <libxml/parser.h>
#include <libxslt/transform.h>
#include <assert.h>

#include "cape.h"
#include "trans.h"

static struct application *application;

static qb_map_t *assembly_map;

static qb_map_t *op_history_map;

static int call_order = 0;

static int cape_debug = 0;

static char crmd_uuid[37];

static xmlDocPtr _pe = NULL;

static xmlDocPtr _config = NULL;

struct operation_history {
	char *rsc_id;
	char *operation;
	uint32_t call_id;
	uint32_t interval;
	enum ocf_exitcode rc;
	uint32_t target_outcome;
	time_t last_run;
	time_t last_rc_change;
	uint32_t graph_id;
	uint32_t action_id;
	char *op_digest;
	struct resource *resource;
};


static void resource_monitor_execute(void * data);

static void recurring_monitor_stop(struct pe_operation *op);

static void schedule_processing(void);

static void op_history_save(struct resource *resource, struct pe_operation *op,
	enum ocf_exitcode ec)
{
	struct operation_history *oh;
	/*
	 * +3 = _ _ and null terminator
	 */
	char buffer[RESOURCE_NAME_MAX + METHOD_NAME_MAX + OP_NAME_MAX + 3];

	qb_enter();

	snprintf(buffer,
		RESOURCE_NAME_MAX + METHOD_NAME_MAX + OP_NAME_MAX + 3,
		"%s_%s_%d", op->rname, op->method, op->interval);

	oh = qb_map_get(op_history_map, buffer);
	if (oh == NULL) {
		oh = (struct operation_history *)calloc(1, sizeof(struct operation_history));
		oh->resource = resource;
		oh->rsc_id = strdup(buffer);
		oh->operation = strdup(op->method);
		oh->target_outcome = op->target_outcome;
		oh->interval = op->interval;
		oh->rc = OCF_PENDING;
		oh->op_digest = strdup(op->op_digest);
		qb_map_put(op_history_map, oh->rsc_id, oh);
	} else
	if (strcmp(oh->op_digest, op->op_digest) != 0) {
		free(oh->op_digest);
		oh->op_digest = op->op_digest;
	}
        if (oh->rc != ec) {
                oh->last_rc_change = time(NULL);
                oh->rc = ec;
        }

        oh->last_run = time(NULL);
        oh->call_id = call_order++;
        oh->graph_id = op->graph_id;
        oh->action_id = op->action_id;

	qb_leave();
}

static void xml_new_int_prop(xmlNode *n, const char *name, int32_t val)
{
	char int_str[36];

	qb_enter();

	snprintf(int_str, 36, "%d", val);
	xmlNewProp(n, BAD_CAST name, BAD_CAST int_str);

	qb_leave();
}

static void xml_new_time_prop(xmlNode *n, const char *name, time_t val)
{
        char int_str[36];

	qb_enter();

        snprintf(int_str, 36, "%d", (int)val);
        xmlNewProp(n, BAD_CAST name, BAD_CAST int_str);

	qb_leave();
}

static void op_history_insert(xmlNode *resource_xml,
	struct operation_history *oh)
{
	xmlNode *op;
	char key[255];
	char magic[255];

	qb_enter();

	op = xmlNewChild(resource_xml, NULL, BAD_CAST "lrm_rsc_op", NULL);

	xmlNewProp(op, BAD_CAST "id", BAD_CAST oh->rsc_id);
	xmlNewProp(op, BAD_CAST "operation", BAD_CAST oh->operation);
	xml_new_int_prop(op, "call-id", oh->call_id);
	xml_new_int_prop(op, "rc-code", oh->rc);
	xml_new_int_prop(op, "interval", oh->interval);
	xml_new_time_prop(op, "last-run", oh->last_run);
	xml_new_time_prop(op, "last-rc-change", oh->last_rc_change);

	snprintf(key, 255, "%d:%d:%d:%s",
		oh->action_id, oh->graph_id, oh->target_outcome, crmd_uuid);

	xmlNewProp(op, BAD_CAST "transition-key", BAD_CAST key);

	snprintf(magic, 255, "0:%d:%s", oh->rc, key);
	xmlNewProp(op, BAD_CAST "transition-magic", BAD_CAST magic);

	xmlNewProp(op, BAD_CAST "op-digest", BAD_CAST oh->op_digest);
	xmlNewProp(op, BAD_CAST "crm-debug-origin", BAD_CAST __func__);
	xmlNewProp(op, BAD_CAST "crm_feature_set", BAD_CAST PE_CRM_VERSION);
	xmlNewProp(op, BAD_CAST "op-status", BAD_CAST "0");
	xmlNewProp(op, BAD_CAST "exec-time", BAD_CAST "0");
	xmlNewProp(op, BAD_CAST "queue-time", BAD_CAST "0");

	qb_leave();
}

static const char * state_str[5][5] = {
	{NULL, "running", NULL, "needs starting", "unrecoverable"},
	{"unknown", NULL, "recovering", "stopped", "unrecoverable"},
	{"unknown", "running", NULL, "stopped", "unrecoverable"},
	{"unknown", "started", NULL, NULL, "unrecoverable"},
	{NULL, NULL, NULL, NULL}
};


static void
resource_state_change_event(void* inst,
			    enum recover_state from,
			    enum recover_state to)
{
	struct resource *r = (struct resource *)inst;

	qb_enter();
	qb_log(LOG_INFO, "Resource: changing state from %d to %d", from, to);

	cape_admin_event_send("my_app", r->assembly, r,
			      state_str[from][to],
			      "bla");
	qb_leave();
}

static void resource_recover_restart(void * inst)
{
	struct resource *resource = (struct resource *)inst;

	qb_enter();

	qb_loop_timer_del(NULL, resource->monitor_timer);

	qb_leave();
}

static void resource_recover_escalate(void * inst)
{
	struct resource *r = (struct resource *)inst;

	qb_enter();

	qb_log(LOG_NOTICE, "Escalating failure of service %s to node %s:%s",
	       r->name, r->assembly->uuid, r->assembly->name);

	qb_loop_timer_del(NULL, r->monitor_timer);

	instance_stop(r->assembly);

	qb_leave();
}

static void node_op_history_clear(struct assembly *assembly)
{
	qb_map_iter_t *iter;
	struct operation_history *oh;
	const char *key;
	struct resource *r;

	qb_enter();

	iter = qb_map_iter_create(op_history_map);
	while ((key = qb_map_iter_next(iter, (void **)&oh)) != NULL) {
		r = oh->resource;

		if (r->assembly == assembly) {
			/* stop the recurring monitor.
			 */
			if (qb_loop_timer_is_running(NULL, r->monitor_timer) &&
			    r->monitor_op) {
				recurring_monitor_stop(r->monitor_op);
			}

			qb_map_rm(op_history_map, key);
			free(oh->rsc_id);
			free(oh->operation);
			free(oh->op_digest);
			free(oh);
		}
	}
	qb_map_iter_free(iter);

	qb_leave();
}

static void
node_state_change_event(void* inst,
			enum recover_state from,
			enum recover_state to)
{
	struct assembly *a = (struct assembly *)inst;

	qb_enter();

	qb_log(LOG_INFO, "Node: changing state from %d to %d", from, to);
	cape_admin_event_send("my_app", a, NULL,
			      state_str[from][to],
			      "bla");
	if (to == RECOVER_STATE_RUNNING) {
		qb_util_stopwatch_stop(a->sw_instance_connected);
		qb_log(LOG_INFO, "Assembly '%s' connected in (%"PRIu64" ms).",
			a->name,
			qb_util_stopwatch_us_elapsed_get(a->sw_instance_connected) / QB_TIME_US_IN_MSEC);
	}
	schedule_processing();
	qb_leave();
}

static void
node_recover_restart(void * inst)
{
	struct assembly *a = (struct assembly *)inst;

	qb_enter();

	transport_disconnect(a);

	node_op_history_clear(a);

	instance_create(a);

	qb_leave();
}

static void
node_recover_escalate(void * inst)
{
//	struct assembly *a = (struct assembly *)inst;

	qb_enter();


	qb_leave();
}

static void
resource_state_set(struct resource *r,
		   struct pe_operation *op,
		   enum ocf_exitcode pe_exitcode)
{
	if (strcmp(op->method, "monitor") == 0) {
		if (pe_exitcode == OCF_OK) {
			recover_state_set(&r->recover,
					  RECOVER_STATE_RUNNING);
		} else if (op->interval == 0) {
			recover_state_set(&r->recover,
					  RECOVER_STATE_STOPPED);
		} else {
			recover_state_set(&r->recover,
					  RECOVER_STATE_FAILED);
		}
	} else if (strcmp(op->method, "start") == 0) {
		if (pe_exitcode == OCF_OK) {
			recover_state_set(&r->recover,
					  RECOVER_STATE_RUNNING);
		} else if (pe_resource_is_hard_error(pe_exitcode)) {
			recover_state_set(&r->recover,
					  RECOVER_STATE_UNRECOVERABLE);
		} else {
			recover_state_set(&r->recover,
					  RECOVER_STATE_UNKNOWN);
		}
	} else if (strcmp(op->method, "stop") == 0) {
		if (pe_exitcode == OCF_OK) {
			recover_state_set(&r->recover,
					  RECOVER_STATE_STOPPED);
		} else if (pe_resource_is_hard_error(pe_exitcode)) {
			recover_state_set(&r->recover,
					  RECOVER_STATE_UNRECOVERABLE);
		} else {
			recover_state_set(&r->recover,
					  RECOVER_STATE_UNKNOWN);
		}
	}
}

void
resource_action_completed(struct pe_operation *op,
			  enum ocf_exitcode pe_exitcode)
{
	uint64_t el;
	struct assembly *a = qb_map_get(assembly_map, op->hostname);;
	struct resource *r = qb_map_get(a->resource_map, op->rname);

	qb_enter();

	op->times_executed++;
	qb_util_stopwatch_stop(op->time_execed);
	el = qb_util_stopwatch_us_elapsed_get(op->time_execed);

	qb_log(LOG_INFO, "%s_%s_%d [%s] on %s rc:[%d/%d] time:[%"PRIu64"/%ums]",
	       op->rname, op->method, op->interval, op->rclass, op->hostname,
	       pe_exitcode, op->target_outcome,
	       el / QB_TIME_US_IN_MSEC, op->timeout);

	if (strstr(op->rname, op->hostname) != NULL) {
		op_history_save(r, op, pe_exitcode);
	}

	if (op->times_executed <= 1) {
		pe_resource_completed(op, pe_exitcode);
	}

	resource_state_set(r, op, pe_exitcode);

	if (pe_exitcode != op->target_outcome) {
		schedule_processing();
	}
	if (op->interval > 0) {
		if (pe_exitcode != op->target_outcome) {
			/* unreference as not used by the timer anymore.
			 */
			pe_resource_unref(op);
		} else {
			qb_loop_timer_add(NULL, QB_LOOP_LOW,
					  op->interval * QB_TIME_NS_IN_MSEC, op,
					  resource_monitor_execute,
					  &r->monitor_timer);
		}
	} else {
		pe_resource_unref(op);
	}

	qb_leave();
}

static void
resource_monitor_execute(void *data)
{
	struct pe_operation *op = (struct pe_operation *)data;
	struct assembly *assembly;
	struct resource *resource;

	qb_enter();

	assembly = qb_map_get(assembly_map, op->hostname);
	resource = qb_map_get(assembly->resource_map, op->rname);

	if (assembly->recover.state != RECOVER_STATE_RUNNING) {
		qb_log(LOG_DEBUG, "can't execute resource in offline state");
		resource_action_completed(op, OCF_UNKNOWN_ERROR);
		return;
	}
	qb_util_stopwatch_start(op->time_execed);
	transport_resource_action(assembly, resource, op);

	qb_leave();
}

static void recurring_monitor_start(struct pe_operation *op)
{
	struct resource * r = (struct resource *)op->resource;

	if (!qb_loop_timer_is_running(NULL, r->monitor_timer)) {
		pe_resource_ref(op);
		r->monitor_op = op;
		resource_monitor_execute(op);
	} else {
		qb_util_stopwatch_stop(op->time_execed);
		pe_resource_completed(op, OCF_OK);
		pe_resource_unref(op);
	}
}

static void recurring_monitor_stop(struct pe_operation *op)
{
	struct resource * r = (struct resource *)op->resource;

	if (qb_loop_timer_is_running(NULL, r->monitor_timer)) {
		qb_loop_timer_del(NULL, r->monitor_timer);
		pe_resource_unref(op);
	}
	r->monitor_op = NULL;
	pe_resource_unref(op);
}

static void op_history_delete(struct pe_operation *op)
{
	struct resource *r;
	qb_map_iter_t *iter;
	const char *key;
	struct operation_history *oh;

	qb_enter();

	/*
	 * Delete this resource from any operational histories
	 */
	iter = qb_map_iter_create(op_history_map);
	while ((key = qb_map_iter_next(iter, (void **)&oh)) != NULL) {
		r = (struct resource *)oh->resource;

		if (r == op->resource) {
			/* stop the recurring monitor.
			 */
			if (qb_loop_timer_is_running(NULL, r->monitor_timer) &&
			    r->monitor_op) {
				recurring_monitor_stop(r->monitor_op);
			}

			qb_map_rm(op_history_map, key);
			free(oh->rsc_id);
			free(oh->operation);
			free(oh->op_digest);
			free(oh);
		}
	}
	qb_map_iter_free(iter);

	qb_util_stopwatch_stop(op->time_execed);
	pe_resource_completed(op, OCF_OK);
	pe_resource_unref(op);

	qb_leave();
}

static void resource_execute_cb(struct pe_operation *op)
{
	struct resource *resource;
	struct assembly *assembly;

	qb_enter();

	assembly = qb_map_get(assembly_map, op->hostname);
	resource = qb_map_get(assembly->resource_map, op->rname);

	if (assembly->recover.state != RECOVER_STATE_RUNNING) {
		qb_log(LOG_DEBUG, "can't execute resource in offline state");
		resource_action_completed(op, OCF_UNKNOWN_ERROR);
		return;
	}
	qb_log(LOG_INFO, "%s_%s_%d [%s] on %s target_rc:%d",
	       op->rname, op->method, op->interval, op->rclass, op->hostname,
	       op->target_outcome);

	qb_util_stopwatch_start(op->time_execed);

	op->resource = resource;
	if (strcmp(op->method, "monitor") == 0) {
		if (strstr(op->rname, op->hostname) != NULL) {
			assert(op->resource);
			if (op->interval > 0) {
				recurring_monitor_start(op);
			} else {
				resource_monitor_execute(op);
			}
		} else {
			qb_util_stopwatch_stop(op->time_execed);
			pe_resource_completed(op, OCF_NOT_RUNNING);
			pe_resource_unref(op);
		}
	} else if (strcmp (op->method, "start") == 0) {
		transport_resource_action(assembly, resource, op);
	} else if (strcmp(op->method, "stop") == 0) {
		if (resource->monitor_op) {
			recurring_monitor_stop(resource->monitor_op);
		}
		transport_resource_action(assembly, resource, op);
	} else if (strcmp(op->method, "delete") == 0) {
		op_history_delete(op);
	} else {
		assert(0);
	}

	qb_leave();
}

static void transition_completed_cb(void* user_data, int32_t result) {
}

static xmlNode *insert_resource(xmlNode *status, struct resource *resource)
{
	xmlNode *resource_xml;

	qb_enter();

	resource_xml = xmlNewChild (status, NULL, BAD_CAST "lrm_resource", NULL);
	xmlNewProp(resource_xml, BAD_CAST "id", BAD_CAST resource->name);
	xmlNewProp(resource_xml, BAD_CAST "type", BAD_CAST resource->type);
	xmlNewProp(resource_xml, BAD_CAST "class", BAD_CAST resource->rclass);
	if (strcmp(resource->rclass, "ocf") == 0) {
		xmlNewProp(resource_xml, BAD_CAST "provider", BAD_CAST resource->rprovider);
	}

	qb_leave();

	return resource_xml;
}

static void insert_status(xmlNode *status, struct assembly *assembly)
{
	struct operation_history *oh;
	xmlNode *resource_xml;
	xmlNode *resources_xml;
	xmlNode *lrm_xml;
	struct resource *resource;
	qb_map_iter_t *iter;

	qb_enter();

	qb_log(LOG_INFO, "Inserting assembly %s", assembly->name);

	xmlNode *node_state = xmlNewChild(status, NULL, BAD_CAST "node_state", NULL);
        xmlNewProp(node_state, BAD_CAST "id", BAD_CAST assembly->uuid);
        xmlNewProp(node_state, BAD_CAST "uname", BAD_CAST assembly->name);
        xmlNewProp(node_state, BAD_CAST "ha", BAD_CAST "active");
        xmlNewProp(node_state, BAD_CAST "expected", BAD_CAST "member");
        xmlNewProp(node_state, BAD_CAST "in_ccm", BAD_CAST "true");
        xmlNewProp(node_state, BAD_CAST "crmd", BAD_CAST "online");

	/* check state*/
	if (assembly->recover.state == RECOVER_STATE_RUNNING) {
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "member");
		qb_log(LOG_INFO, "Assembly '%s' marked as member",
			assembly->name);

	} else {
		xmlNewProp(node_state, BAD_CAST "join", BAD_CAST "pending");
		qb_log(LOG_INFO, "Assembly '%s' marked as pending",
			assembly->name);
	}
	lrm_xml = xmlNewChild(node_state, NULL, BAD_CAST "lrm", NULL);
	resources_xml = xmlNewChild(lrm_xml, NULL, BAD_CAST "lrm_resources", NULL);
	iter = qb_map_iter_create(op_history_map);
	while ((qb_map_iter_next(iter, (void **)&oh)) != NULL) {
		resource = oh->resource;

		if (strstr(resource->name, assembly->name) == NULL) {
			continue;
		}
		resource_xml = insert_resource(resources_xml, oh->resource);
		op_history_insert(resource_xml, oh);
	}
	qb_map_iter_free(iter);

	qb_leave();
}

static void process(void)
{
	xmlNode *cur_node;
	xmlNode *status;
	int rc;
	struct assembly *assembly;
	qb_map_iter_t *iter;
	static xmlNode *pe_root;

	qb_enter();

	/*
	 * Remove status descriptor
	 */
	pe_root = xmlDocGetRootElement(_pe);
	for (cur_node = pe_root->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE &&
			strcmp((char*)cur_node->name, "status") == 0) {

			xmlUnlinkNode(cur_node);
			xmlFreeNode(cur_node);
			break;
		}
	}

	status = xmlNewChild(pe_root, NULL, BAD_CAST "status", NULL);
	iter = qb_map_iter_create(assembly_map);
	while ((qb_map_iter_next(iter, (void **)&assembly)) != NULL) {
		insert_status(status, assembly);
	}
	qb_map_iter_free(iter);

	rc = pe_process_state(_pe, resource_execute_cb,
			      transition_completed_cb,
			      NULL, cape_debug);

	if (rc != 0) {
		schedule_processing();
	}

	qb_leave();
}

static void process_job(void *data)
{
	qb_enter();

	if (pe_is_busy_processing()) {
		schedule_processing();
	} else {
		process();
	}

	qb_leave();
}

static void schedule_processing(void)
{
	qb_enter();

	qb_loop_job_add(NULL, QB_LOOP_LOW, NULL, process_job);

	qb_leave();
}

static void resource_create(xmlNode *cur_node, struct assembly *assembly)
{
	struct resource *resource;
	char *name;
	char *type;
	char *rclass;
	char *rprovider;
	/* 6 = rsc__ and terminator */
	char resource_name[ASSEMBLY_NAME_MAX + RESOURCE_NAME_MAX + 6];
	char *escalation_failures;
	char *escalation_period;

	qb_enter();

	resource = calloc(1, sizeof (struct resource));
	name = (char*)xmlGetProp(cur_node, BAD_CAST "name");
	snprintf(resource_name, ASSEMBLY_NAME_MAX + RESOURCE_NAME_MAX + 6,
		"rsc_%s_%s", assembly->name, name);
	resource->name = strdup(resource_name);
	type = (char*)xmlGetProp(cur_node, BAD_CAST "type");
	resource->type = strdup(type);
	rclass = (char*)xmlGetProp(cur_node, BAD_CAST "class");
	resource->rclass = strdup(rclass);

	rprovider = (char*)xmlGetProp(cur_node, BAD_CAST "provider");
	if (rprovider) {
		resource->rprovider = strdup(rprovider);
	} else {
		resource->rprovider = NULL;
	}
	escalation_failures = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_failures");
	escalation_period = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_period");

	recover_init(&resource->recover,
		    escalation_failures, escalation_period,
		    resource_recover_restart,
		    resource_recover_escalate,
		    resource_state_change_event);
	resource->recover.instance = resource;

	resource->assembly = assembly;
	qb_map_put(assembly->resource_map, resource->name, resource);

	qb_leave();
}

static void resources_create(xmlNode *cur_node, struct assembly *assembly)
{
	qb_enter();

	for (; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		resource_create(cur_node, assembly);
	}

	qb_leave();
}

static void assembly_create(xmlNode *cur_node)
{
	struct assembly *assembly;
	char *name;
	char *uuid;
	xmlNode *child_node;
	char *escalation_failures;
	char *escalation_period;

	qb_enter();

	assembly = calloc(1, sizeof (struct assembly));
	name = (char*)xmlGetProp(cur_node, BAD_CAST "name");
	assembly->name = strdup(name);
	uuid = (char*)xmlGetProp(cur_node, BAD_CAST "uuid");
	assembly->uuid = strdup(uuid);
	assembly->resource_map = qb_skiplist_create();
	assembly->sw_instance_create = qb_util_stopwatch_create();
	assembly->sw_instance_connected = qb_util_stopwatch_create();
	assembly->application = application;

	escalation_failures = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_failures");
	escalation_period = (char*)xmlGetProp(cur_node, BAD_CAST "escalation_period");

	recover_init(&assembly->recover,
		    escalation_failures, escalation_period,
		    node_recover_restart,
		    node_recover_escalate,
		    node_state_change_event);
	assembly->recover.instance = assembly;

	instance_create(assembly);
	qb_map_put(assembly_map, name, assembly);

	for (child_node = cur_node->children; child_node;
		child_node = child_node->next) {
		if (child_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (strcmp((char*)child_node->name, "services") == 0) {
			resources_create(child_node->children, assembly);
		}
	}

	qb_leave();
}

static void assemblies_create(xmlNode *xml)
{
	xmlNode *cur_node;

	qb_enter();

        for (cur_node = xml; cur_node; cur_node = cur_node->next) {
                if (cur_node->type != XML_ELEMENT_NODE) {
                        continue;
                }
		assembly_create(cur_node);
	}

	qb_leave();
}

static void
parse_and_load(void)
{
	char *name;
	char *uuid;
	xmlNode *cur_node;
	xmlNode *dep_node;
        xsltStylesheetPtr ss;
	const char *params[1];

	qb_enter();

	ss = xsltParseStylesheetFile(BAD_CAST "/usr/share/pacemaker-cloud/cf2pe.xsl");
	params[0] = NULL;

        _pe = xsltApplyStylesheet(ss, _config, params);
        xsltFreeStylesheet(ss);
        dep_node = xmlDocGetRootElement(_config);

	application = calloc(1, sizeof(struct application));

	name = (char*)xmlGetProp(dep_node, BAD_CAST "name");
	application->name = strdup(name);
	uuid = (char*)xmlGetProp(dep_node, BAD_CAST "uuid");
	application->uuid = strdup(uuid);

        for (cur_node = dep_node->children; cur_node;
             cur_node = cur_node->next) {
                if (cur_node->type == XML_ELEMENT_NODE) {
                        if (strcmp((char*)cur_node->name, "assemblies") == 0) {
				assemblies_create(cur_node->children);
                        }
                }
        }

	qb_leave();
}

void
cape_load_from_buffer(const char *buffer)
{
	qb_enter();

	_config = xmlParseMemory(buffer, strlen(buffer));
	parse_and_load();

	qb_leave();
}

int
cape_load(const char * name)
{
	char cfg[PATH_MAX];

	qb_enter();

	snprintf(cfg, PATH_MAX, "/var/run/%s.xml", name);
	if (access(cfg, R_OK) != 0) {
		return -errno;
	}
	_config = xmlParseFile(cfg);
	parse_and_load();

	qb_leave();

	return 0;
}

void
cape_init(int debug)
{
	uuid_t uuid_temp_id;

	qb_enter();

	cape_debug = debug;

	uuid_generate(uuid_temp_id);
	uuid_unparse(uuid_temp_id, crmd_uuid);

	assembly_map = qb_skiplist_create();
	op_history_map = qb_skiplist_create();

	qb_leave();
}

void cape_exit(void)
{
	struct assembly *assembly;
	qb_map_iter_t *iter;

	qb_enter();

	iter = qb_map_iter_create(assembly_map);
	while ((qb_map_iter_next(iter, (void **)&assembly)) != NULL) {
		transport_disconnect(assembly);
	}
	qb_map_iter_free(iter);

	qb_leave();
}
