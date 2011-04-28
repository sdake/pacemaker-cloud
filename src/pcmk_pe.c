/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of cpe.
 *
 * cpe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * cpe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cpe.  If not, see <http://www.gnu.org/licenses/>.
 */

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

/* The return codes for the status operation are not the same for other operatios - go figure
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
static void * run_user_data = NULL;

enum ocf_exitcode pe_get_ocf_exitcode(const char *action, int lsb_exitcode)
{
    if(action != NULL && strcmp("status", action) == 0) {
	switch(lsb_exitcode) {
	    case LSB_STATUS_OK:			return OCF_OK;
	    case LSB_STATUS_VAR_PID:		return OCF_NOT_RUNNING;
	    case LSB_STATUS_VAR_LOCK:		return OCF_NOT_RUNNING;
	    case LSB_STATUS_NOT_RUNNING:	return OCF_NOT_RUNNING;
	    case LSB_STATUS_NOT_INSTALLED:	return OCF_UNKNOWN_ERROR;
	    default:
		return OCF_UNKNOWN_ERROR;
	}

    } else if(lsb_exitcode > LSB_NOT_RUNNING) {
	return OCF_UNKNOWN_ERROR;
    }

    /* For non-status operations, the LSB and OCF share error code meaning for rc <= 7 */
    return (enum ocf_exitcode)lsb_exitcode;
}

static gboolean
exec_pseudo_action(crm_graph_t *graph, crm_action_t *action)
{
	action->confirmed = TRUE;
	update_graph(graph, action);
	return TRUE;
}

static gboolean
exec_rsc_action(crm_graph_t *graph, crm_action_t *action)
{
	int rc = 0;
	int target_outcome = 0;
	lrm_op_t *op = NULL;
	struct pe_operation pe_op;
	const char *target_rc_s = crm_meta_value(action->params, XML_ATTR_TE_TARGET_RC);
	xmlNode *action_rsc = first_named_child(action->xml, XML_CIB_TAG_RESOURCE);
	char *node = crm_element_value_copy(action->xml, XML_LRM_ATTR_TARGET);

	if(safe_str_eq(crm_element_value(action->xml, "operation"), "probe_complete")) {
		qb_log(LOG_INFO, "Skipping %s op for %s\n",
		       crm_element_value(action->xml, "operation"), node);
		goto done;
	}

	if(action_rsc == NULL) {
		crm_log_xml_err(action->xml, "Bad");
		crm_free(node);
		return FALSE;
	}

	pe_op.hostname = node;
	pe_op.user_data = run_user_data;
	pe_op.rname = ID(action_rsc);
	pe_op.rclass = crm_element_value(action_rsc, XML_AGENT_ATTR_CLASS);
	pe_op.rprovider = crm_element_value(action_rsc, XML_AGENT_ATTR_PROVIDER);
	pe_op.rtype = crm_element_value(action_rsc, XML_ATTR_TYPE);

	if(target_rc_s != NULL) {
		target_outcome = crm_parse_int(target_rc_s, "0");
	}
	op = convert_graph_action(NULL, action, 0, target_outcome);

	pe_op.method = op->op_type;
	pe_op.params = op->params;
	pe_op.timeout = op->timeout;
	pe_op.interval = op->interval;

	rc = run_fn(&pe_op);
	if (rc != target_outcome) {
		qb_log(LOG_ERR, "rsc %s failed %d != %d", op->op_type, rc, target_outcome);
		action->failed = TRUE;
		graph->abort_priority = INFINITY;
	}
	free_lrm_op(op);
done:
	crm_free(node);
	action->confirmed = TRUE;
	update_graph(graph, action);
	return TRUE;
}

static gboolean
exec_crmd_action(crm_graph_t *graph, crm_action_t *action)
{
	action->confirmed = TRUE;
	update_graph(graph, action);
	return TRUE;
}

#define STATUS_PATH_MAX 512
static gboolean
exec_stonith_action(crm_graph_t *graph, crm_action_t *action)
{
#if 0
	int rc = 0;
	char xpath[STATUS_PATH_MAX];
	char *target = crm_element_value_copy(action->xml, XML_LRM_ATTR_TARGET);
	xmlNode *cib_node = modify_node(global_cib, target, FALSE);
	crm_xml_add(cib_node, XML_ATTR_ORIGIN, __FUNCTION__);
	CRM_ASSERT(cib_node != NULL);

	quiet_log(" * Fencing %s\n", target);
	rc = global_cib->cmds->replace(global_cib, XML_CIB_TAG_STATUS, cib_node, cib_sync_call|cib_scope_local);
	CRM_ASSERT(rc == cib_ok);

	snprintf(xpath, STATUS_PATH_MAX, "//node_state[@uname='%s']/%s", target, XML_CIB_TAG_LRM);
	rc = global_cib->cmds->delete(global_cib, xpath, NULL, cib_xpath|cib_sync_call|cib_scope_local);

	snprintf(xpath, STATUS_PATH_MAX, "//node_state[@uname='%s']/%s", target, XML_TAG_TRANSIENT_NODEATTRS);
	rc = global_cib->cmds->delete(global_cib, xpath, NULL, cib_xpath|cib_sync_call|cib_scope_local);
#endif
	action->confirmed = TRUE;
	update_graph(graph, action);
	//    free_xml(cib_node);
	//    crm_free(target);
	return TRUE;
}

static int32_t
process_graph(pe_working_set_t *data_set)
{
	crm_graph_t *transition = NULL;
	enum transition_status graph_rc = -1;

	crm_graph_functions_t exec_fns =
	{
		exec_pseudo_action,
		exec_rsc_action,
		exec_crmd_action,
		exec_stonith_action,
	};

	set_graph_functions(&exec_fns);

	qb_log(LOG_DEBUG, "Executing cluster transition");
	transition = unpack_graph(data_set->graph, crm_system_name);
	print_graph(LOG_DEBUG, transition);

	do {
		graph_rc = run_graph(transition);

	} while(graph_rc == transition_active);

	if(graph_rc != transition_complete) {
		qb_log(LOG_ERR, "Transition failed: %s", transition_status(graph_rc));
		print_graph(LOG_ERR, transition);
	}
	destroy_graph(transition);
	if(graph_rc != transition_complete) {
		qb_log(LOG_ERR, "An invalid transition was produced\n");
	}

	if(graph_rc != transition_complete) {
		return graph_rc;
	}
	return 0;
}


int32_t
pe_process_state(xmlNode *xml_input, pe_resource_execute_t fn,
		 void *user_data)
{
	char *msg_buffer = NULL;
	FILE* fh;
	pe_working_set_t data_set;
	int32_t rc = 0;

	run_fn = fn;
	run_user_data = user_data;

	set_working_set_defaults(&data_set);

	/* print input */
	msg_buffer = dump_xml_formatted(xml_input);
	fh = fopen("/home/asalkeld/work/fluffy/cloud-policy-engine/pe-in.xml", "a");
	if (fh) {
		fprintf(fh, "%s\n", msg_buffer);
		fflush(fh);
		fclose(fh);
	}
	crm_free(msg_buffer);

	/* calculate output */
	do_calculations(&data_set, xml_input, NULL);

	/* print output */
	msg_buffer = dump_xml_formatted(data_set.graph);
	fh = fopen("/home/asalkeld/work/fluffy/cloud-policy-engine/pe-out.xml", "a");
	if (fh) {
		fprintf(fh, "%s\n", msg_buffer);
		fflush(fh);
		fclose(fh);
	}
	crm_free(msg_buffer);

	rc = process_graph(&data_set);

	// we don't want to free the input xml
	data_set.input = NULL;
	cleanup_alloc_calculations(&data_set);

	return rc;
}

