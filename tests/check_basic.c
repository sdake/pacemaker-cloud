/*
 * Copyright (C) 2012 Red Hat, Inc.
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

#include <check.h>

#include <qb/qbdefs.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>

#include "cape.h"
#include "trans.h"

static const char * test1_conf = "\
<deployable name=\"foo\" uuid=\"123456\" monitor=\"tester\" username=\"me\">\
  <assemblies>\
    <assembly name=\"bar\" uuid=\"7891011\" escalation_failures=\"3\" escalation_period=\"10\">\
      <services>\
        <service name=\"angus\" provider=\"me\" class=\"lsb\" type=\"httpd\" monitor_interval=\"1\" escalation_period=\"1\" escalation_failures=\"2\">\
          <paramaters>\
	    <paramater name=\"not\" value=\"this\"/>\
          </paramaters>\
	</service>\
      </services>\
    </assembly>\
  </assemblies>\
  <constraints/>\
</deployable>\
";

static int inst_up = 0;
static int bail_count = 0;

int32_t instance_create(struct assembly *assembly)
{
	qb_loop_job_add(NULL, QB_LOOP_LOW, assembly, instance_state_detect);
	return 0;
}

void instance_state_detect(void *data)
{
	struct assembly * a = (struct assembly *)data;
	if (!inst_up) {
		ta_connect(a);
		inst_up = 1;
	}
}

int instance_stop(char *image_name)
{
	return 0;
}


void ta_del(void *ta)
{
}

struct job_holder {
	struct assembly *a;
	struct resource *r;
	struct pe_operation *op;
};

static void resource_action_completion_cb(void *data)
{
	struct job_holder *j = (struct job_holder*)data;

	resource_action_completed(j->op, 0);
	free(j);
	bail_count++;
	if (bail_count > 10) {
		qb_loop_stop(NULL);
	}
}

void
ta_resource_action(struct assembly * a,
		   struct resource *r,
		   struct pe_operation *op)
{
	struct job_holder *j = malloc(sizeof(struct job_holder));
	j->a = a;
	j->r = r;
	j->op = op;
	qb_loop_job_add(NULL, QB_LOOP_MED, j, resource_action_completion_cb);
}

void*
ta_connect(struct assembly * a)
{
	node_state_changed(a, NODE_STATE_RUNNING);
	return NULL;
}

START_TEST(test_basic_run)
{
	qb_loop_t *loop = qb_loop_create();

	cape_init();

	cape_load_from_buffer(test1_conf);

	qb_loop_run(loop);
}
END_TEST


static Suite *
basic_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("basic");

	tc = tcase_create("basic_run");
	tcase_add_test(tc, test_basic_run);
	suite_add_tcase(s, tc);


	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = basic_suite();
	SRunner *sr = srunner_create(s);

	qb_log_init("check", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_INFO);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
