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

#include "cape.h"

static int num_restarts;
static int num_escalations;

static void
_restart_cb(void* inst)
{
	num_restarts++;
	qb_log(LOG_TRACE, "restart %d", num_restarts);
}

static void
_escalate_cb(void* inst)
{
	num_escalations++;
	qb_log(LOG_TRACE, "escalate %d", num_escalations);
}


START_TEST(test_recover)
{
	struct recover r;

	recover_init(&r, "3", "1", _restart_cb, _escalate_cb, NULL);
	r.instance = &r;

	num_restarts = 0;
	num_escalations = 0;

	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	ck_assert_int_eq(num_restarts, 2);
	ck_assert_int_eq(num_escalations, 1);

	/* after an escalation we start again */
	num_restarts = 0;
	num_escalations = 0;

	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	ck_assert_int_eq(num_escalations, 0);
	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	qb_log(LOG_DEBUG, "escalate here");
	ck_assert_int_eq(num_restarts, 2);
	ck_assert_int_eq(num_escalations, 1);

	num_restarts = 0;
	num_escalations = 0;

	usleep(300000);
	/* it should only count from the first failure
	 * after the escalation
	 * (so below)
	 */
	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	ck_assert_int_eq(num_restarts, 1);
	ck_assert_int_eq(num_escalations, 0);
	usleep(600000);

	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	ck_assert_int_eq(num_restarts, 2);
	ck_assert_int_eq(num_escalations, 0);
	usleep(600000);

	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	/* no escalation as it's been over a second */
	ck_assert_int_eq(num_restarts, 3);
	ck_assert_int_eq(num_escalations, 0);

	usleep(300000);

	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	ck_assert_int_eq(num_restarts, 3);
	ck_assert_int_eq(num_escalations, 1);

	recover_state_set(&r, RECOVER_STATE_RUNNING);
	recover_state_set(&r, RECOVER_STATE_FAILED);
	ck_assert_int_eq(num_restarts, 4);
	ck_assert_int_eq(num_escalations, 1);
}
END_TEST

static Suite *
recover_suite(void)
{
	TCase *tc;
	Suite *s = suite_create("recover");

	tc = tcase_create("recover");
	tcase_add_test(tc, test_recover);
	suite_add_tcase(s, tc);

	return s;
}

int32_t main(void)
{
	int32_t number_failed;

	Suite *s = recover_suite();
	SRunner *sr = srunner_create(s);

	qb_log_init("check", LOG_USER, LOG_EMERG);
	qb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log_format_set(QB_LOG_STDERR, "[%6p] %f:%l %b");

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
