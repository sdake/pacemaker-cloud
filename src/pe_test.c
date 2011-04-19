
/*
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <glib.h>
#include <libxml/parser.h>

#include <crm/transition.h>
#include <crm/pengine/status.h>

gboolean use_stdin = FALSE;
gboolean do_simulation = FALSE;
gboolean inhibit_exit = FALSE;
gboolean all_actions = FALSE;
extern xmlNode * do_calculations(pe_working_set_t *data_set,
				 xmlNode *xml_input, ha_time_t *now);
extern void cleanup_calculations(pe_working_set_t *data_set);
extern xmlNode* get_object_root(const char *object_type, xmlNode *the_root);

extern gboolean show_utilization;
extern gboolean show_scores;

char *use_date = NULL;

FILE *dot_strm = NULL;
#define DOT_PREFIX "PE_DOT: "
/* #define DOT_PREFIX "" */

#define dot_write(fmt...) if (dot_strm != NULL) {	\
	fprintf(dot_strm, fmt);				\
	fprintf(dot_strm, "\n");			\
} else {						\
	crm_debug(DOT_PREFIX""fmt);			\
}

static void
init_dotfile(void)
{
	dot_write(" digraph \"g\" {");
	/* 	dot_write("	size = \"30,30\""); */
	/* 	dot_write("	graph ["); */
	/* 	dot_write("		fontsize = \"12\""); */
	/* 	dot_write("		fontname = \"Times-Roman\""); */
	/* 	dot_write("		fontcolor = \"black\""); */
	/* 	dot_write("		bb = \"0,0,398.922306,478.927856\""); */
	/* 	dot_write("		color = \"black\""); */
	/* 	dot_write("	]"); */
	/* 	dot_write("	node ["); */
	/* 	dot_write("		fontsize = \"12\""); */
	/* 	dot_write("		fontname = \"Times-Roman\""); */
	/* 	dot_write("		fontcolor = \"black\""); */
	/* 	dot_write("		shape = \"ellipse\""); */
	/* 	dot_write("		color = \"black\""); */
	/* 	dot_write("	]"); */
	/* 	dot_write("	edge ["); */
	/* 	dot_write("		fontsize = \"12\""); */
	/* 	dot_write("		fontname = \"Times-Roman\""); */
	/* 	dot_write("		fontcolor = \"black\""); */
	/* 	dot_write("		color = \"black\""); */
	/* 	dot_write("	]"); */
}

static char *
create_action_name(action_t *action)
{
	char *action_name = NULL;
	const char *action_host = NULL;
	if (action->node) {
		action_host = action->node->details->uname;
		action_name = crm_concat(action->uuid, action_host, ' ');

	} else if (is_set(action->flags, pe_action_pseudo)) {
		action_name = crm_strdup(action->uuid);

	} else {
		action_host = "<none>";
		action_name = crm_concat(action->uuid, action_host, ' ');
	}
	if (safe_str_eq(action->task, RSC_CANCEL)) {
		char *tmp_action_name = action_name;
		action_name = crm_concat("Cancel", tmp_action_name, ' ');
		crm_free(tmp_action_name);
	}

	return action_name;
}

static struct crm_option long_options[] = {
	/* Top-level Options */
	{"help",           0, 0, '?', "This text"},
	{"version",        0, 0, '$', "Version information"  },
	{"verbose",        0, 0, 'V', "Increase debug output\n"},

	{"simulate",    0, 0, 'S', "Simulate the transition's execution to find invalid graphs\n"},
	{"show-scores", 0, 0, 's', "Display resource allocation scores"},
	{"show-utilization", 0, 0, 'U', "Display utilization information"},
	{"all-actions", 0, 0, 'a', "Display all possible actions - even ones not part of the transition graph"},

	{"xml-text",    1, 0, 'X', "Retrieve XML from the supplied string"},
	{"xml-file",    1, 0, 'x', "Retrieve XML from the named file"},
	/* {"xml-pipe",    0, 0, 'p', "Retrieve XML from stdin\n"}, */

	{"save-input",  1, 0, 'I', "\tSave the input to the named file"},
	{"save-graph",  1, 0, 'G', "\tSave the transition graph (XML format) to the named file"},
	{"save-dotfile",1, 0, 'D', "Save the transition graph (DOT format) to the named file\n"},

	{0, 0, 0, 0}
};

int
main(int argc, char **argv)
{
	GListPtr lpc = NULL;
	gboolean process = TRUE;
	gboolean all_good = TRUE;
	enum transition_status graph_rc = -1;
	crm_graph_t *transition = NULL;
	ha_time_t *a_date = NULL;

	xmlNode * cib_object = NULL;
	int argerr = 0;
	int flag;

	char *msg_buffer = NULL;
	gboolean optional = FALSE;
	pe_working_set_t data_set;

	const char *source = NULL;
	const char *xml_file = NULL;
	const char *dot_file = NULL;
	const char *graph_file = NULL;
	const char *input_file = NULL;

	/* disable glib's fancy allocators that can't be free'd */
	GMemVTable vtable;

	vtable.malloc = malloc;
	vtable.realloc = realloc;
	vtable.free = free;
	vtable.calloc = calloc;
	vtable.try_malloc = malloc;
	vtable.try_realloc = realloc;

	g_mem_set_vtable(&vtable);

	crm_log_init_quiet(NULL, LOG_CRIT, FALSE, FALSE, argc, argv);
	crm_set_options("V?$XD:G:I:Lwx:d:aSsU", "[-?Vv] -[Xxp] {other options}", long_options,
			"Calculate the cluster's response to the supplied cluster state\n");

	while (1) {
		int option_index = 0;
		flag = crm_get_option(argc, argv, &option_index);
		if (flag == -1)
			break;

		switch(flag) {
		case 'S':
			do_simulation = TRUE;
			break;
		case 'a':
			all_actions = TRUE;
			break;
		case 'w':
			inhibit_exit = TRUE;
			break;
		case 'X':
			use_stdin = TRUE;
			break;
		case 's':
			show_scores = TRUE;
			break;
		case 'U':
			show_utilization = TRUE;
			break;
		case 'x':
			xml_file = optarg;
			break;
		case 'd':
			use_date = optarg;
			break;
		case 'D':
			dot_file = optarg;
			break;
		case 'G':
			graph_file = optarg;
			break;
		case 'I':
			input_file = optarg;
			break;
		case 'V':
			cl_log_enable_stderr(TRUE);
			alter_debug(DEBUG_INC);
			break;
		case '$':
		case '?':
			crm_help(flag, 0);
			break;
		default:
			fprintf(stderr, "Option -%c is not yet supported\n", flag);
			++argerr;
			break;
		}
	}

	if (optind < argc) {
		printf("non-option ARGV-elements: ");
		while (optind < argc) {
			printf("%s ", argv[optind++]);
		}
		printf("\n");
	}

	if (optind > argc) {
		++argerr;
	}

	if (argerr) {
		crm_err("%d errors in option parsing", argerr);
		crm_help('?', 1);
	}

	update_all_trace_data(); /* again, so we see which trace points got updated */

	if (xml_file != NULL) {
		source = xml_file;
		cib_object = filename2xml(xml_file);

	} else if (use_stdin) {
		source = "stdin";
		cib_object = filename2xml(NULL);
	}

	if (cib_object == NULL && source) {
		fprintf(stderr, "Could not parse configuration input from: %s\n", source);
		return 4;

	} else if (cib_object == NULL) {
		fprintf(stderr, "No configuration specified\n");
		crm_help('?', 1);
	}

	if (get_object_root(XML_CIB_TAG_STATUS, cib_object) == NULL) {
		create_xml_node(cib_object, XML_CIB_TAG_STATUS);
	}

	if (cli_config_update(&cib_object, NULL, FALSE) == FALSE) {
		free_xml(cib_object);
		return -1 /*cib_STALE*/;
	}

	if (validate_xml(cib_object, NULL, FALSE) != TRUE) {
		free_xml(cib_object);
		return /*cib_dtd_validation*/ -1;
	}

	if (input_file != NULL) {
		FILE *input_strm = fopen(input_file, "w");
		if (input_strm == NULL) {
			crm_perror(LOG_ERR,"Could not open %s for writing", input_file);
		} else {
			msg_buffer = dump_xml_formatted(cib_object);
			if (fprintf(input_strm, "%s\n", msg_buffer) < 0) {
				crm_perror(LOG_ERR,"Write to %s failed", input_file);
			}
			fflush(input_strm);
			fclose(input_strm);
			crm_free(msg_buffer);
		}
	}

	if (use_date != NULL) {
		a_date = parse_date(&use_date);
		log_date(LOG_WARNING, "Set fake 'now' to",
			 a_date, ha_log_date|ha_log_time);
		log_date(LOG_WARNING, "Set fake 'now' to (localtime)",
			 a_date, ha_log_date|ha_log_time|ha_log_local);
	}

	set_working_set_defaults(&data_set);
	if (process) {
		if (show_scores && show_utilization) {
			fprintf(stdout, "Allocation scores and utilization information:\n");
		} else if (show_scores) {
			fprintf(stdout, "Allocation scores:\n");
		} else if (show_utilization) {
			fprintf(stdout, "Utilization information:\n");
		}
		do_calculations(&data_set, cib_object, a_date);
	}

	msg_buffer = dump_xml_formatted(data_set.graph);
	if (safe_str_eq(graph_file, "-")) {
		fprintf(stdout, "%s\n", msg_buffer);
		fflush(stdout);
	} else if (graph_file != NULL) {
		FILE *graph_strm = fopen(graph_file, "w");
		if (graph_strm == NULL) {
			crm_perror(LOG_ERR,"Could not open %s for writing", graph_file);
		} else {
			if (fprintf(graph_strm, "%s\n\n", msg_buffer) < 0) {
				crm_perror(LOG_ERR,"Write to %s failed", graph_file);
			}
			fflush(graph_strm);
			fclose(graph_strm);
		}
	}
	crm_free(msg_buffer);

cleanup:
	cleanup_alloc_calculations(&data_set);
	crm_log_deinit();

	/* required for MallocDebug.app */
	if (inhibit_exit) {
		GMainLoop*  mainloop = g_main_new(FALSE);
		g_main_run(mainloop);
	}

	if (all_good) {
		return 0;
	}
	return graph_rc;
}
