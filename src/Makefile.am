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

AM_CFLAGS = -Wall

xsldir = $(datadir)/pacemaker-cloud
xsl_DATA = cf2pe.xsl

EXTRA_DIST = $(xsl_DATA)

SUBDIRS = pcloudsh

sbin_PROGRAMS = cped dped dped-ssh
noinst_PROGRAMS = cpe-tool2 pe_test
noinst_HEADERS = assembly.h assembly_am.h assembly_pm.h config.h cpe_agent.h \
	dpe_agent.h cpe_httpd.h mainloop.h vmlauncher.h \
	common_agent.h  config_loader.h  deployable.h  init-dbus.h  pcmk_pe.h \
	schema.xml org/pacemakercloud/QmfPackage.cpp \
	org/pacemakercloud/QmfPackage.h resource.h qmf_object.h \
	qmf_multiplexer.h qmf_job.h qmf_agent.h cpe_impl.h pcmk_pe_ssh.h \
	pcmk_pe_ssh.c

qmfauto_path = org/pacemakercloud
qmfauto_c = $(qmfauto_path)/QmfPackage.cpp

$(qmfauto_c): schema.xml
	qmf-gen -2 schema.xml

cped_SOURCES  = libinit.c mainloop.cpp $(qmfauto_c) common_agent.cpp \
	        cpe_agent.cpp cpe_impl.cpp cpe_httpd.cpp cpe_agent_register.cpp

cped_CPPFLAGS = $(libqb_CFLAGS) $(dbus_glib_1_CFLAGS) $(glib_CFLAGS) $(qmf_CFLAGS) \
		  -I$(qmfauto_path) $(libxml2_CFLAGS)

cped_LDFLAGS  = $(libqb_LIBS) $(dbus_glib_1_LIBS) $(qmf_LIBS) $(glib_LIBS) \
		$(libmicrohttpd_LIBS) $(libcurl_LIBS) $(libxml2_LIBS)

dped_SOURCES  = pcmk_pe.c mainloop.cpp $(qmfauto_c) common_agent.cpp \
		deployable.cpp assembly.cpp assembly_am.cpp assembly_pm.cpp \
		config_loader.cpp dpe_agent.cpp vmlauncher.cpp \
		resource.cpp qmf_multiplexer.cpp qmf_object.cpp qmf_agent.cpp

dped_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(qmf_CFLAGS) $(libxml2_CFLAGS) \
		-I$(qmfauto_path) $(pcmk_CFLAGS) $(libxslt_CFLAGS) $(uuid_CFLAGS)

dped_LDFLAGS  = $(libqb_LIBS) $(qmf_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
		$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS)

dped_ssh_SOURCES = dped_ssh.c pcmk_pe_ssh.c

dped_ssh_CFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(libxml2_CFLAGS) \
	$(pcmk_CFLAGS) $(libxslt_CFLAGS) $(uuid_LIBS) $(libdeltacloud_LIBS)

dped_ssh_LDFLAGS  = $(libqb_LIBS) $(qmf_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libdeltacloud_LIBS)

cpe_tool2_SOURCES = cpe-tool2.c libinit.c
cpe_tool2_CPPFLAGS = $(dbus_glib_1_CFLAGS)
cpe_tool2_LDFLAGS = $(dbus_glib_1_LIBS) $(libqb_LIBS)

pe_test_SOURCES = pe_test.c
pe_test_CPPFLAGS = -Wall $(glib_CFLAGS) $(libxml2_CFLAGS) $(pcmk_CFLAGS)
pe_test_LDADD = $(pcmk_LIBS) $(glib_LIBS) $(libxml2_LIBS)

