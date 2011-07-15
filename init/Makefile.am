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

upstartconfdir = $(sysconfdir)/init
upstartconf_DATA = pcmkc-dped.conf

systemdconfdir = /lib/systemd/system
systemdconf_DATA = pcmkc-dped@.service pcmkc-cped.service \
		   pcmkc-vmlauncher.service pcmkc-qpidd.service

EXTRA_DIST = $(upstartconf_DATA) $(systemdconf_DATA)

