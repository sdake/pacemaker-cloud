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
#include "pcmk_pe.h"

#include <inttypes.h>
#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qblist.h>
#include <qb/qbloop.h>
#include <qb/qblog.h>
#include <assert.h>

#include "cape.h"
#include "trans.h"

void
recover_init(struct recover* r,
	    const char * escalation_failures,
	    const char * escalation_period,
	    recover_restart_fn_t recover_restart,
	    recover_escalate_fn_t recover_escalate,
	    recover_state_changing_fn_t recover_state_changing)
{
	long val;
	char *endptr;

	qb_enter();

	if (escalation_failures == NULL) {
		r->num_failures = -1;
	} else {
		val = strtol(escalation_failures, &endptr, 10);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) ||
		    (endptr == escalation_failures)) {
			r->num_failures = -1;
		} else {
			r->num_failures = val;
		}
	}

	if (escalation_period == NULL) {
		r->failure_period = -1;
	} else {
		val = strtol(escalation_period, &endptr, 10);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) ||
		    (endptr == escalation_period)) {
			r->failure_period = -1;
		} else {
			r->failure_period = val;
		}
	}

	r->restart = recover_restart;
	r->escalate = recover_escalate;
	r->state_changing = recover_state_changing;
	r->state = RECOVER_STATE_UNKNOWN;

	r->sw = NULL;
	if (r->failure_period > 0 && r->num_failures > 0) {
		r->sw = qb_util_stopwatch_create();
		qb_util_stopwatch_split_ctl(r->sw,
					    r->num_failures,
					    QB_UTIL_SW_OVERWRITE);
	}

	qb_leave();
}

void
recover_state_set(struct recover* r, enum recover_state state)
{
	int escalating = QB_FALSE;

	if (r->state == state) {
		return;
	}

	if (r->state_changing) {
		r->state_changing(r->instance, r->state, state);
	}
	r->state = state;

	if (r->state != RECOVER_STATE_FAILED) {
		return;
	}

	qb_enter();

	if (r->num_failures > 0 && r->failure_period > 0) {
		uint64_t diff;
		float p;
		int last;
		qb_util_stopwatch_split(r->sw);

		last = qb_util_stopwatch_split_last(r->sw);
		qb_log(LOG_TRACE, "split number %d/%d", last+1, r->num_failures);
		if (last >= r->num_failures - 1) {
			diff = qb_util_stopwatch_time_split_get(r->sw, last,
								last - (r->num_failures - 1));
			p = diff;
			p /= QB_TIME_US_IN_SEC;
			qb_log(LOG_TRACE, "split time %f/%d", p, r->failure_period);
			if (p <= r->failure_period) {
				escalating = QB_TRUE;
				qb_util_stopwatch_start(r->sw);
			}
		}
	}
	if (escalating) {
		r->escalate(r->instance);
	} else {
		r->restart(r->instance);
	}
	qb_leave();
}

