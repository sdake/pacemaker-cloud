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

AM_CFLAGS = -Wall

upstartconfdir = $(sysconfdir)/init
upstartconf_DATA = dped.conf

systemdconfdir = /lib/systemd/system
systemdconf_DATA = dped@.service

sbin_PROGRAMS = cped dped
noinst_PROGRAMS = cpe-tool2 pe_test

qmfauto_path = $(abs_builddir)/org/cloudpolicyengine
qmfauto_c = $(qmfauto_path)/QmfPackage.cpp

qpid_LIBS = -lqmf -lqmf2 -lqpidtypes -lqpidcommon -lqpidmessaging

$(qmfauto_c): schema.xml
	qmf-gen -2 schema.xml

cped_SOURCES  = libinit.c mainloop.cpp $(qmfauto_c) common_agent.cpp \
	        cpe_agent.cpp

cped_CPPFLAGS = $(libqb_CFLAGS) $(dbus_glib_1_CFLAGS) $(glib_CFLAGS) $(qpid_CFLAGS) \
		  -I$(qmfauto_path)

cped_LDFLAGS  = $(libqb_LIBS) $(dbus_glib_1_LIBS) $(qpid_LIBS) $(glib_LIBS)

dped_SOURCES  = mainloop.cpp $(qmfauto_c) common_agent.cpp dpe_agent.cpp \
	        deployable.cpp assembly.cpp config_loader.cpp

dped_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(qpid_CFLAGS) $(libxml2_CFLAGS) \
		-I$(qmfauto_path)

dped_LDFLAGS  = $(libqb_LIBS) $(qpid_LIBS) $(glib_LIBS) $(libxml2_LIBS)

cpe_tool2_SOURCES = cpe-tool2.c libinit.c
cpe_tool2_CPPFLAGS = $(dbus_glib_1_CFLAGS)
cpe_tool2_LDFLAGS = $(dbus_glib_1_LIBS) $(libqb_LIBS)

pe_test_SOURCES = pe_test.c
pe_test_CPPFLAGS = -Wall $(glib_CFLAGS) $(libxml2_CFLAGS) \
		   -I/usr/include/pacemaker \
		   -I/usr/include/heartbeat

pe_test_LDADD = -lcrmcommon \
		-lpe_status \
		-lpengine \
		-lcib \
		-ltransitioner \
		-lplumb \
		$(glib_LIBS) \
		$(libxml2_LIBS)
