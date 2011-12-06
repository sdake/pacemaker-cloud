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

#include <qb/qblog.h>

#include "vmlauncher.h"
#include "deployable.h"

static void
my_method_response(QmfAsyncRequest* ar,
		   qpid::types::Variant::Map out_args,
		   enum QmfObject::rpc_result rc)
{
	Assembly *a = (Assembly *)ar->user_data;

	if (rc == QmfObject::RPC_OK) {
		if (ar->method == "status") {
			string st = out_args["status"];
			a->status_response(st);
		} else {
			qb_log(LOG_DEBUG, "%s result: %d", ar->method.c_str(), rc);
		}
	} else {
		qb_log(LOG_ERR, "%s result: %d", ar->method.c_str(), rc);
	}
}

VmLauncher::VmLauncher(Deployable *dep): _dep(dep)
{
	_vm_launcher.query_set("{class:Vmlauncher, package:org.pacemakercloud}");
	_vm_launcher.method_response_handler_set(my_method_response);
	_dep->qmf_object_add(&_vm_launcher);
}

void
VmLauncher::start(Assembly *a)
{
	qpid::types::Variant::Map in_args;
	in_args["name"] = a->name_get();
	in_args["uuid"] = a->uuid_get();
	_vm_launcher.method_call_async("start", in_args, a, 5000);
}

void
VmLauncher::stop(Assembly *a)
{
	qpid::types::Variant::Map in_args;
	in_args["name"] = a->name_get();
	in_args["uuid"] = a->uuid_get();
	_vm_launcher.method_call_async("stop", in_args, a, 5000);
}

void
VmLauncher::restart(Assembly *a)
{
	qpid::types::Variant::Map in_args;
	in_args["name"] = a->name_get();
	in_args["uuid"] = a->uuid_get();
	_vm_launcher.method_call_async("restart", in_args, a, 5000);
}

void
VmLauncher::status(Assembly *a)
{
	qpid::types::Variant::Map in_args;
	in_args["name"] = a->name_get();
	in_args["uuid"] = a->uuid_get();
	_vm_launcher.method_call_async("status", in_args, a, 5000);
}
