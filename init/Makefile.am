# Copyright (C) 2010-2011 Red Hat, Inc.
#
# Authors: Angus Salkeld <asalkeld@redhat.com>
#
# This file is part of pacemaker-cloud.
#
# pacemaker-cloud is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# pacemaker-cloud is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with pacemaker-cloud.  If not, see <http://www.gnu.org/licenses/>.

MAINTAINERCLEANFILES	= Makefile.in

sysconfsysconfdir = $(sysconfdir)/sysconfig
sysconfsysconf_DATA = pacemaker-cloud

upstartconfdir = $(sysconfdir)/init
upstartconf_DATA = pcloud-dped.conf

systemdconfdir = /lib/systemd/system
systemdconf_DATA = pcloud-dped@.service pcloud-cped.service \
		   pcloud-vmlauncher.service pcloud-qpidd.service

EXTRA_DIST = $(upstartconf_DATA) $(systemdconf_DATA) $(sysconfsysconf_DATA)

