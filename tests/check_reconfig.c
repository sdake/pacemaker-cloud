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

static const char *test1_conf = "\
<deployable name=\"foo\" uuid=\"123456\" monitor=\"tester\" username=\"me\">\
  <assemblies>\
    <assembly name=\"bar\" uuid=\"7891011\" escalation_failures=\"3\" escalation_period=\"10\">\
      <services>\
        <service name=\"angus\" provider=\"me\" class=\"lsb\" type=\"httpd\" monitor_interval=\"1\" escalation_period=\"-1\" escalation_failures=\"-1\">\
          <configure_executable url=\"http://random.com/bla/wordpress/wordpress-http.sh\"/>\
          <parameters>\
            <parameter name=\"wp_name\" type=\"scalar\"><value>wordpress</value></parameter>\
            <parameter name=\"wp_user\" type=\"scalar\"><value>wordpress</value></parameter>\
            <parameter name=\"wp_pw\" type=\"scalar\"><value>wordpress</value></parameter>\
            <parameter name=\"mysql_ip\" type=\"scalar\"><reference assembly=\"victim\" parameter=\"ipaddress\"/></parameter>\
            <parameter name=\"mysql_hostname\" type=\"scalar\"><reference assembly=\"victim\" parameter=\"hostname\"/></parameter>\
          </parameters>\
	</service>\
      </services>\
    </assembly>\
    <assembly name=\"victim\" uuid=\"7891411\" escalation_failures=\"3\" escalation_period=\"10\">\
      <services>\
        <service name=\"andy\" provider=\"me\" class=\"lsb\" type=\"mysql\" monitor_interval=\"1\" escalation_period=\"-1\" escalation_failures=\"-1\">\
	</service>\
      </services>\
    </assembly>\
  </assemblies>\
  <constraints>\
    <service_dependancy id=\"1\" first=\"rsc_victim_andy\" then=\"rsc_bar_angus\"/>\
  </constraints>\
</deployable>\
";

enum resource_test_seq {
	RSEQ_INIT,
	RSEQ_MON_0,
	RSEQ_MON_1,
	RSEQ_MON_2,
	RSEQ_START_0,
	RSEQ_START_1,
	RSEQ_START_2,
	RSEQ_MON_REPEAT_1,
	RSEQ_MON_REPEAT_2,
	RSEQ_MON_REPEAT_3,
	RSEQ_MON_REPEAT_4,
	RSEQ_MON_REPEAT_5,
	RSEQ_MON_REPEAT_6,
	RSEQ_MON_REPEAT_FAIL,
	RSEQ_STOP_1,
	RSEQ_MON_3,
	RSEQ_STOP_2,
	RSEQ_START_3,
	RSEQ_STOP_3,
	RSEQ_MON_4,
	RSEQ_START_4,
	RSEQ_MON_REPEAT_7,
	RSEQ_MON_REPEAT_8,
};

struct job_holder {
	struct assembly *a;
	struct resource *r;
	struct pe_operation *op;
};

static int test_ip = 0;
static int test_seq = RSEQ_INIT;
static int is_node_test = 0;
static int seen_my_param = 0;

static void
instance_state_detect(void *data)
{
	struct assembly *a = (struct assembly *)data;
	recover_state_set(&a->recover, RECOVER_STATE_RUNNING);
}

int32_t
instance_create(struct assembly *a)
{
	char ip[12];

	qb_log(LOG_INFO, "starting instance (seq %d)", test_seq);

	snprintf(ip, 12, "1.2.3.%d", ++test_ip);
	a->address = strdup(ip);

	if (test_seq >= RSEQ_MON_REPEAT_FAIL) {
		qb_loop_timer_add(NULL, QB_LOOP_LOW, 1 * QB_TIME_NS_IN_SEC, a,
				  instance_state_detect, NULL);
	} else {
		qb_loop_job_add(NULL, QB_LOOP_LOW, a, instance_state_detect);
	}

	return 0;
}

int
instance_stop(struct assembly *a)
{
	qb_log(LOG_INFO, "stopping instance");
	return 0;
}

void
transport_disconnect(struct assembly *a)
{
}

static int32_t
log_ocf_envs(const char *key, void *value, void *user_data)
{
	if (strcmp(key, "executable_url") == 0 &&
	    strcmp(value,
		   "http://random.com/bla/wordpress/wordpress-http.sh") == 0) {
		seen_my_param++;
	}
	if (strcmp(key, "wp_user") == 0 && strcmp(value, "wordpress") == 0) {
		seen_my_param++;
	}
	qb_log(LOG_INFO, "%s: %s", key, value);
	return 0;
}

static void
resource_action_completion_cb(void *data)
{
	struct job_holder *j = (struct job_holder *)data;

	test_seq++;
	if (test_seq > RSEQ_MON_REPEAT_FAIL &&
	    strcmp("bar", j->a->name) == 0 && j->op->interval > 0) {
		resource_action_completed(j->op, OCF_OK);
		free(j);
		test_seq--;
		return;
	}
	if (strcmp("ocf", j->op->rclass) == 0) {
		seen_my_param = 0;
		qb_map_foreach(j->op->params, log_ocf_envs, NULL);
		ck_assert_int_eq(seen_my_param, 2);
	}

	switch (test_seq) {
	case RSEQ_MON_0:
	case RSEQ_MON_1:
	case RSEQ_MON_2:
	case RSEQ_MON_3:
		ck_assert_int_eq(j->op->interval, 0);
		ck_assert_str_eq(j->op->method, "monitor");
		resource_action_completed(j->op, OCF_NOT_RUNNING);
		break;
	case RSEQ_START_0:
	case RSEQ_START_1:
	case RSEQ_START_2:
	case RSEQ_START_3:
	case RSEQ_START_4:
		ck_assert_str_eq(j->op->method, "start");
		resource_action_completed(j->op, OCF_OK);
		break;
	case RSEQ_MON_REPEAT_1:
	case RSEQ_MON_REPEAT_2:
	case RSEQ_MON_REPEAT_3:
	case RSEQ_MON_REPEAT_4:
		ck_assert_int_eq(j->op->interval, 1000);
		ck_assert_str_eq(j->op->method, "monitor");
		resource_action_completed(j->op, OCF_OK);
		break;
	case RSEQ_MON_REPEAT_5:
	case RSEQ_MON_REPEAT_6:
	case RSEQ_MON_REPEAT_FAIL:
		ck_assert_int_eq(j->op->interval, 1000);
		ck_assert_str_eq(j->op->method, "monitor");
		if (strcmp("victim", j->a->name) == 0) {
			test_seq = RSEQ_MON_REPEAT_FAIL;
			qb_log(LOG_NOTICE, "killing the victim node");
			recover_state_set(&j->a->recover, RECOVER_STATE_FAILED);
		} else {
			resource_action_completed(j->op, OCF_OK);
		}
		break;
	case RSEQ_STOP_1:
	case RSEQ_STOP_2:
	case RSEQ_STOP_3:
		ck_assert_str_eq(j->op->method, "stop");
		resource_action_completed(j->op, OCF_OK);
		break;
	default:
		qb_loop_stop(NULL);
		break;
	}

	free(j);
}

void
transport_resource_action(struct assembly *a,
			  struct resource *r, struct pe_operation *op)
{
	struct job_holder *j = malloc(sizeof(struct job_holder));
	j->a = a;
	j->r = r;
	j->op = op;
	qb_loop_job_add(NULL, QB_LOOP_MED, j, resource_action_completion_cb);
}

void *
transport_connect(struct assembly *a)
{
	return NULL;
}

START_TEST(test_restart_node)
{
	qb_loop_t *loop = qb_loop_create();

	is_node_test = 1;

	cape_init(1);

	cape_load_from_buffer(test1_conf);

	qb_loop_run(loop);
}

END_TEST static Suite *
reconfig_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("reconfig");

	tc = tcase_create("restart_node");
	tcase_add_test(tc, test_restart_node);
	tcase_set_timeout(tc, 30);
	suite_add_tcase(s, tc);

	return s;
}

int32_t
main(void)
{
	int32_t number_failed;

	Suite *s = reconfig_suite();
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
