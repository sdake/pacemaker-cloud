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

sbin_PROGRAMS = cped dped
noinst_PROGRAMS = cpe-tool2

qmfauto_path = $(abs_builddir)/org/cloudpolicyengine
qmfauto_c = $(qmfauto_path)/QmfPackage.cpp



$(qmfauto_c): schema.xml
	qmf-gen -2 schema.xml


cped_SOURCES  = libupstart.c mainloop.c $(qmfauto_c) common_agent.cpp \
	        cpe_agent.cpp

cped_CPPFLAGS = $(dbus_glib_1_CFLAGS) $(glib_CFLAGS) $(qpid_CFLAGS) \
		  -I$(qmfauto_path)

cped_LDFLAGS  = $(dbus_glib_1_LIBS) $(qpid_LIBS) -lqmf -lqpidtypes \
	        -lqpidcommon -lqmf2 $(glib_LIBS)


dped_SOURCES  = mainloop.c $(qmfauto_c) common_agent.cpp dpe_agent.cpp \
	        deployable.cpp assembly.cpp config_loader.cpp

dped_CPPFLAGS = $(glib_CFLAGS) $(qpid_CFLAGS) $(libxml2_CFLAGS) \
		-I$(qmfauto_path)

dped_LDFLAGS  = $(qpid_LIBS) -lqmf -lqpidtypes -lqpidcommon -lqmf2 \
		-lqpidmessaging $(glib_LIBS) $(libxml2_LIBS)

cpe_tool2_SOURCES = cpe-tool2.c libupstart.c
cpe_tool2_CPPFLAGS = $(dbus_glib_1_CFLAGS)
cpe_tool2_LDFLAGS = $(dbus_glib_1_LIBS)
