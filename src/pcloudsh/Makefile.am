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

rscsdir = $(datadir)/pacemaker-cloud/resource_templates
rscs_DATA = resource_templates/httpd.xml

bin_SCRIPTS = pcloudsh vmlauncher

pcloudshdir = $(pythondir)/pcloudsh
pcloudsh_PYTHON = assembly.py deployable.py ifconfig.py cpe.py \
                  eventreceiver.py eventrunner.py jeos.py resource.py \
				  pcmkconfig.py __init__.py

jeosdir = $(localstatedir)/lib/pacemaker-cloud/jeos/
jeos_DATA = F14-x86_64-jeos.tdl F14-x86_64-jeos-assembly.tdl F15-x86_64-jeos.tdl \
            F15-x86_64-jeos-assembly.tdl rhel61-x86_64-jeos.tdl \
            rhel61-x86_64-jeos-assembly.tdl 

assydir = $(localstatedir)/lib/pacemaker-cloud/assemblies
assy_DATA = assy1.tdl assy1-F14.tdl assy1-F15.tdl

EXTRA_DIST = $(rscs_DATA) $(jeos_DATA) $(assy_DATA) $(bin_SCRIPTS) $(pcloudsh_PYTHON)
