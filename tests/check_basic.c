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

/*
 * Test purpose:
 * start the cluster up
 * confirm:
 * - it checks for the resource presence
 * - it starts the resource
 * start monitoring
 * confirm:
 * - we get monitor requests
 * inject a failure in a node
 * confirm:
 * - node gets restarted.
 * - resource gets restarted
 */


static const char * test1_conf = "\
<deployable name=\"foo\" uuid=\"123456\" monitor=\"tester\" username=\"me\">\
  <assemblies>\
    <assembly name=\"bar\" uuid=\"7891011\" escalation_failures=\"3\" escalation_period=\"10\">\
      <services>\
        <service name=\"angus\" provider=\"me\" class=\"lsb\" type=\"httpd\" monitor_interval=\"1\" escalation_period=\"-1\" escalation_failures=\"-1\">\
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


enum resource_test_seq {
	RSEQ_INIT,
	RSEQ_MON_0,
	RSEQ_START_1,
	RSEQ_MON_REPEAT_1,
	RSEQ_MON_REPEAT_2,
	RSEQ_MON_REPEAT_3,
	RSEQ_MON_REPEAT_4,
	RSEQ_MON_REPEAT_FAIL,
	RSEQ_MON_1,
	RSEQ_STOP_1,
	RSEQ_START_2,
	RSEQ_MON_REPEAT_5,
	RSEQ_MON_REPEAT_6,
};
static int inst_up = 0;
static int test_seq = RSEQ_INIT;
static int is_node_test = 0;

int32_t instance_create(struct assembly *a)
{
	qb_log(LOG_INFO, "starting instance (seq %d)", test_seq);

	if (is_node_test && test_seq >= RSEQ_MON_REPEAT_FAIL) {
		qb_loop_timer_add(NULL, QB_LOOP_LOW, 1 * QB_TIME_NS_IN_SEC, a,
				  instance_state_detect, NULL);
	} else {
		qb_loop_job_add(NULL, QB_LOOP_LOW, a, instance_state_detect);
	}

	return 0;
}

void instance_state_detect(void *data)
{
	struct assembly * a = (struct assembly *)data;
	if (!inst_up) {
		recover_state_set(&a->recover, RECOVER_STATE_RUNNING);
		inst_up = 1;
	}
}

int instance_stop(struct assembly *a)
{
	qb_log(LOG_INFO, "stopping instance");
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

	/*
	 * node failure causes an op history flush
	 * RSEQ_MON_REPEAT_FAIL
	 * RSEQ_MON_1
	 * RSEQ_START_2
	 * resource test will look like:
	 * RSEQ_MON_REPEAT_FAIL
	 * RSEQ_STOP_1
	 * RSEQ_START_2
	 */
	if (is_node_test && test_seq == RSEQ_MON_1) {
		test_seq = RSEQ_START_2;
	} else if (!is_node_test && test_seq == RSEQ_MON_REPEAT_FAIL) {
		test_seq = RSEQ_STOP_1;
	} else {
		test_seq++;
	}

	switch (test_seq) {
	case RSEQ_MON_0:
	case RSEQ_MON_1:
		ck_assert_int_eq(j->op->interval, 0);
		ck_assert_str_eq(j->op->method, "monitor");
		resource_action_completed(j->op, OCF_NOT_RUNNING);
		break;
	case RSEQ_START_1:
		ck_assert_str_eq(j->op->method, "start");
		resource_action_completed(j->op, OCF_OK);
		break;
	case RSEQ_MON_REPEAT_1:
	case RSEQ_MON_REPEAT_2:
	case RSEQ_MON_REPEAT_3:
	case RSEQ_MON_REPEAT_4:
	case RSEQ_MON_REPEAT_5:
	case RSEQ_MON_REPEAT_6:
		ck_assert_int_eq(j->op->interval, 1000);
		ck_assert_str_eq(j->op->method, "monitor");
		resource_action_completed(j->op, OCF_OK);
		break;
	case RSEQ_MON_REPEAT_FAIL:
		ck_assert_int_eq(j->op->interval, 1000);
		ck_assert_str_eq(j->op->method, "monitor");
		if (is_node_test) {
			inst_up = 0;
			recover_state_set(&j->a->recover, RECOVER_STATE_FAILED);
		} else {
			resource_action_completed(j->op, OCF_NOT_RUNNING);
		}
		break;
	case RSEQ_STOP_1:
		ck_assert_str_eq(j->op->method, "stop");
		resource_action_completed(j->op, OCF_OK);
		break;
	case RSEQ_START_2:
		ck_assert_str_eq(j->op->method, "start");
		resource_action_completed(j->op, OCF_OK);
		break;
	default:
		qb_loop_stop(NULL);
		break;
	}

	free(j);
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
	return NULL;
}

START_TEST(test_restart_resource)
{
	qb_loop_t *loop = qb_loop_create();

	is_node_test = 0;
	cape_init();

	cape_load_from_buffer(test1_conf);

	qb_loop_run(loop);
}
END_TEST

START_TEST(test_restart_node)
{
	qb_loop_t *loop = qb_loop_create();

	is_node_test = 1;
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

	tc = tcase_create("restart_resource");
	tcase_add_test(tc, test_restart_resource);
	tcase_set_timeout(tc, 20);
	suite_add_tcase(s, tc);

	tc = tcase_create("restart_node");
	tcase_add_test(tc, test_restart_node);
	tcase_set_timeout(tc, 30);
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
	qb_log_format_set(QB_LOG_STDERR, "[%6p] %f:%l %b");

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
