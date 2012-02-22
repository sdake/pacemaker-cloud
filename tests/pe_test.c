
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

#include <glib.h>
#include <libxml/parser.h>
#include <crm/transition.h>
#include <crm/pengine/status.h>

extern xmlNode * do_calculations(pe_working_set_t *data_set,
				 xmlNode *xml_input, ha_time_t *now);
extern void cleanup_alloc_calculations(pe_working_set_t *data_set);
extern xmlNode* get_object_root(const char *object_type, xmlNode *the_root);

static struct crm_option long_options[] = {
	/* Top-level Options */
	{"help",           0, 0, '?', "This text"},
	{"version",        0, 0, '$', "Version information"  },
	{"verbose",        0, 0, 'V', "Increase debug output\n"},
	{"xml-file",    1, 0, 'x', "Retrieve XML from the named file"},
	{0, 0, 0, 0}
};

int
main(int argc, char **argv)
{
	xmlNode * cib_object = NULL;
	int argerr = 0;
	int flag;

	char *msg_buffer = NULL;
	pe_working_set_t data_set;
	const char *xml_file = NULL;

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
	crm_set_options("V?$G:x:", "[-?Vv] -[x] {other options}", long_options,
			"Calculate the cluster's response to the supplied cluster state\n");

	while (1) {
		int option_index = 0;
		flag = crm_get_option(argc, argv, &option_index);
		if (flag == -1)
			break;

		switch(flag) {
		case 'x':
			xml_file = optarg;
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
		cib_object = filename2xml(xml_file);
	}

	if (cib_object == NULL && xml_file) {
		fprintf(stderr, "Could not parse configuration input from: %s\n", xml_file);
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

	set_working_set_defaults(&data_set);
	do_calculations(&data_set, cib_object, NULL);

	msg_buffer = dump_xml_formatted(data_set.graph);
	fprintf(stdout, "%s\n", msg_buffer);
	fflush(stdout);

	crm_free(msg_buffer);
	cleanup_alloc_calculations(&data_set);
	crm_log_deinit();

	return 0;
}
