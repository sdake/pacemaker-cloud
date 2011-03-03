# Copyright (C) 2010 Red Hat, Inc.
#
# Authors: Angus Salkeld <asalkeld@redhat.com>
#
# This file is part of cpe.
#
# cpe is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# cpe is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with cpe.  If not, see <http://www.gnu.org/licenses/>.

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf

EXTRA_DIST		= autogen.sh

upstartconfdir = $(sysconfdir)/init
upstartconf_DATA = dped.conf

sbin_PROGRAMS = dped
bin_PROGRAMS = cpe-tool2

qmfauto_path = $(abs_builddir)/org/cloudpolicyengine
qmfauto_c = $(qmfauto_path)/Package.cpp $(qmfauto_path)/Dpe.cpp $(qmfauto_path)/Deployable.cpp

$(qmfauto_c): schema.xml
	qmf-gen --v2-style schema.xml

dped_SOURCES = mainloop.c $(qmfauto_c) agent.cpp dpe_agent.cpp deployable_agent.cpp assembly.cpp
dped_CPPFLAGS = $(GLIB_CFLAGS) $(QPID_CFLAGS) -I$(qmfauto_path)
dped_LDADD = $(QPID_LIBS) -lqmf -lqpidtypes -lqpidcommon -lqmf2 $(GLIB_LIBS)

#cpe_tool_SOURCES = cpe-tool.cpp
#cpe_tool_CPPFLAGS = $(GLIB_CFLAGS) $(QPID_CFLAGS)
#cpe_tool_LDADD = $(QPID_LIBS) -lqmf2

cpe_tool2_SOURCES = cpe-tool2.c libupstart.c
cpe_tool2_CPPFLAGS = $(DBUS_GLIB_1_CFLAGS)
cpe_tool2_LDADD = $(DBUS_GLIB_1_LIBS)
