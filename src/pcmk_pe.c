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
#include <assert.h>
#include <string.h>
#include <glib.h>
#include <libxml/parser.h>
#include <crm/transition.h>
#include <crm/pengine/status.h>
#undef LOG_TRACE
#include <qb/qblog.h>
#include "mainloop.h"
#include "pcmk_pe.h"

#define LSB_PENDING -1
#define	LSB_OK  0
#define	LSB_UNKNOWN_ERROR 1
#define	LSB_INVALID_PARAM 2
#define	LSB_UNIMPLEMENT_FEATURE 3
#define	LSB_INSUFFICIENT_PRIV 4
#define	LSB_NOT_INSTALLED 5
#define	LSB_NOT_CONFIGURED 6
#define	LSB_NOT_RUNNING 7

/* The return codes for the status operation are not the same for other operations
 * - go figure
 */
#define LSB_STATUS_OK 0
#define LSB_STATUS_VAR_PID 1
#define LSB_STATUS_VAR_LOCK 2
#define LSB_STATUS_NOT_RUNNING 3
#define LSB_STATUS_NOT_INSTALLED 4
#define LSB_STATUS_UNKNOWN_ERROR 199

extern xmlNode * do_calculations(pe_working_set_t *data_set,
				 xmlNode *xml_input, ha_time_t *now);

extern void cleanup_alloc_calculations(pe_working_set_t *data_set);

static pe_resource_execute_t run_fn = NULL;
static pe_transition_completed_t completed_fn = NULL;
static void * run_user_data = NULL;
static pe_working_set_t *working_set = NULL;
static int graph_updated = FALSE;

enum ocf_exitcode pe_resource_ocf_exitcode_get(struct pe_operation *op, int lsb_exitcode)
{
	if (strcmp(op->rclass, "lsb") == 0 && strcmp("monitor", op->method) == 0) {
		switch(lsb_exitcode) {
		case LSB_STATUS_OK:		return OCF_OK;
		case LSB_STATUS_VAR_PID:	return OCF_NOT_RUNNING;
		case LSB_STATUS_VAR_LOCK:	return OCF_NOT_RUNNING;
		case LSB_STATUS_NOT_RUNNING:	return OCF_NOT_RUNNING;
		case LSB_STATUS_NOT_INSTALLED:	return OCF_UNKNOWN_ERROR;
		default:
						return OCF_UNKNOWN_ERROR;
		}

	} else if (lsb_exitcode > LSB_NOT_RUNNING) {
		return OCF_UNKNOWN_ERROR;
	}

	/* For non-status operations, the LSB and OCF share error code meaning
	 * for rc <= 7
	 */
	return (enum ocf_exitcode)lsb_exitcode;
}

void pe_resource_ref(struct pe_operation *op)
{
	op->refcount++;
}

void pe_resource_unref(struct pe_operation *op)
{
	op->refcount--;

	if (op->refcount == 0) {
		crm_free(op->hostname);
		crm_free(op->rprovider);
		crm_free(op->rtype);
		crm_free(op->rclass);
		free(op->method);
		free(op->rname);
		free(op);
	}
}

void
pe_resource_completed(struct pe_operation *op, uint32_t return_code)
{
	crm_graph_t *graph = op->graph;
	crm_action_t *action = op->action;

	qb_enter();

	if (working_set == NULL) {
		return;
	}

	if (return_code != op->target_outcome) {
		action->failed = TRUE;
		graph->abort_priority = INFINITY;
	}
	action->confirmed = TRUE;
	update_graph(graph, action);
	graph_updated = TRUE;

}

static void
dup_attr(gpointer key, gpointer value, gpointer user_data)
{
	g_hash_table_replace(user_data, crm_strdup(key), crm_strdup(value));
}

static gboolean
exec_rsc_action(crm_graph_t *graph, crm_action_t *action)
{
	lrm_op_t *op = NULL;
	struct pe_operation *pe_op;
	const char *target_rc_s = crm_meta_value(action->params, XML_ATTR_TE_TARGET_RC);
	xmlNode *action_rsc = first_named_child(action->xml, XML_CIB_TAG_RESOURCE);
	char *node = crm_element_value_copy(action->xml, XML_LRM_ATTR_TARGET);
	const char *tmp_provider;
	xmlNode *params_all;

	qb_enter();

	if (safe_str_eq(crm_element_value(action->xml, "operation"), "probe_complete")) {
		qb_log(LOG_INFO, "Skipping rsc %s op for %s\n",
		       crm_element_value(action->xml, "operation"), node);
		crm_free(node);
		action->confirmed = TRUE;
		update_graph(graph, action);
		graph_updated = TRUE;
		return TRUE;
	}

	if (action_rsc == NULL) {
		crm_log_xml_err(action->xml, "Bad");
		crm_free(node);
		return FALSE;
	}

	pe_op = calloc(1, sizeof(struct pe_operation));
	pe_op->refcount = 1;
	pe_op->hostname = node;
	pe_op->user_data = run_user_data;
	pe_op->rname = strdup(ID(action_rsc));
	pe_op->rclass = strdup(crm_element_value(action_rsc, XML_AGENT_ATTR_CLASS));
	tmp_provider = crm_element_value(action_rsc, XML_AGENT_ATTR_PROVIDER);
	if (tmp_provider) {
		pe_op->rprovider = strdup(tmp_provider);
	}
	pe_op->rtype = strdup(crm_element_value(action_rsc, XML_ATTR_TYPE));

	if (target_rc_s != NULL) {
		pe_op->target_outcome = crm_parse_int(target_rc_s, "0");
	}
	op = convert_graph_action(NULL, action, 0, pe_op->target_outcome);

	params_all = create_xml_node(NULL, XML_TAG_PARAMS);
	g_hash_table_foreach(op->params, hash2field, params_all);
/*
 * TODO at some point.
	g_hash_table_foreach(action->extra, hash2field, params_all);
	g_hash_table_foreach(rsc->parameters, hash2field, params_all);
	g_hash_table_foreach(action->meta, hash2metafield, params_all);
*/
	filter_action_parameters(params_all, PE_CRM_VERSION);
	pe_op->op_digest = calculate_operation_digest(params_all, PE_CRM_VERSION);

	pe_op->method = strdup(op->op_type);

	pe_op->params = g_hash_table_new_full(g_str_hash_traditional, g_str_equal,
					      g_hash_destroy_str, g_hash_destroy_str);

	if (op->params != NULL) {
		g_hash_table_foreach(op->params, dup_attr, pe_op->params);
	}

	pe_op->timeout = op->timeout;
	pe_op->interval = op->interval;
	pe_op->action = action;
	pe_op->graph = graph;
	pe_op->action_id = action->id;
	pe_op->graph_id = graph->id;

	free_lrm_op(op);
	free_xml(params_all);

	run_fn(pe_op);
	return TRUE;
}

static gboolean
exec_pseudo_action(crm_graph_t *graph, crm_action_t *action)
{
	qb_log(LOG_INFO, "Skipping pseudo %s op \n",
	       crm_element_value(action->xml, "operation"));

	action->confirmed = TRUE;
	update_graph(graph, action);
	graph_updated = TRUE;
	return TRUE;
}

static gboolean
exec_crmd_action(crm_graph_t *graph, crm_action_t *action)
{
	qb_log(LOG_INFO, "Skipping crmd %s op \n",
	       crm_element_value(action->xml, "operation"));

	action->confirmed = TRUE;
	update_graph(graph, action);
	graph_updated = TRUE;
	return TRUE;
}

static gboolean
exec_stonith_action(crm_graph_t *graph, crm_action_t *action)
{
	char *target = crm_element_value_copy(action->xml, XML_LRM_ATTR_TARGET);

	qb_log(LOG_WARNING, "Skipping STONITH %s op (not fencing %s)\n",
	       crm_element_value(action->xml, "operation"), target);
	crm_free(target);

	action->confirmed = TRUE;
	update_graph(graph, action);
	graph_updated = TRUE;
	return TRUE;
}

static crm_graph_functions_t graph_exec_fns =
{
	exec_pseudo_action,
	exec_rsc_action,
	exec_crmd_action,
	exec_stonith_action,
};

static void
process_next_job(void* data)
{
	crm_graph_t *transition = (crm_graph_t *)data;
	enum transition_status graph_rc;
	qb_loop_timer_handle th;

	if (!graph_updated) {
		mainloop_timer_add(1000,
				   transition,
				   process_next_job, &th);
		return;
	}
	qb_enter();

	graph_updated = FALSE;
	graph_rc = run_graph(transition);

	qb_log(LOG_INFO, "run_graph returned: %s", transition_status(graph_rc));

	if (graph_rc == transition_active || graph_rc == transition_pending) {
		mainloop_timer_add(1000,
				   transition,
				   process_next_job, &th);
		return;
	}

	if (graph_rc == transition_complete) {
		qb_log(LOG_INFO, "Transition Completed");
	} else {
		qb_log(LOG_ERR, "Transition failed: %s",
		       transition_status(graph_rc));
	}
	destroy_graph(transition);

	// we don't want to free the input xml
	working_set->input = NULL;
	cleanup_alloc_calculations(working_set);
	free(working_set);
	working_set = NULL;

	completed_fn(run_user_data, graph_rc);

	return;
}

int32_t
pe_is_busy_processing(void)
{
	if (working_set != NULL) {
		return TRUE;
	}
	return FALSE;
}


void
cl_log(int priority, const char * fmt, ...)
{
	va_list		ap;
	char		buf[512];

	buf[512-1] = '\0';
	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	qb_log_from_external_source(__func__, __FILE__,
				    "%s", priority, __LINE__, 3, buf);
}

int32_t
pe_process_state(xmlNode *xml_input,
		 pe_resource_execute_t exec_fn,
		 pe_transition_completed_t done_fn,
		 void *user_data)
{
	crm_graph_t *transition = NULL;

	if (working_set) {
		qb_log(LOG_ERR, "Transition already in progress");
		return -EEXIST;
	}

	set_crm_log_level(LOG_INFO);
	//set_crm_log_level(12);

	assert(validate_xml(xml_input, NULL, FALSE) == TRUE);

	qb_log(LOG_INFO, "Executing deployable transition");

	working_set = calloc(1, sizeof(pe_working_set_t));
	run_fn = exec_fn;
	completed_fn = done_fn;
	run_user_data = user_data;
	set_graph_functions(&graph_exec_fns);

	set_working_set_defaults(working_set);

	/* calculate output */
	do_calculations(working_set, xml_input, NULL);

	transition = unpack_graph(working_set->graph, __func__);
	//print_graph(LOG_INFO, transition);

	graph_updated = TRUE;
	mainloop_job_add(QB_LOOP_HIGH,
			 transition,
			 process_next_job);
	qb_leave();
	return 0;
}

