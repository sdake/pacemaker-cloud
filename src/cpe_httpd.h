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

#ifndef _CPE_HTTPD_H_
#define _CPE_HTTPD_H_

#include <microhttpd.h>
#include <cpe_impl.h>

class CpeHttpd {
private:
	struct MHD_Daemon *daemon;
	CpeImpl *impl;
	const int port;

public:
	CpeHttpd(int port);
	~CpeHttpd();

	void run(void);
	void impl_set(CpeImpl *impl);
	CpeImpl * impl_get(void) {return impl;};
};

#endif // _CPE_HTTPD_H_
