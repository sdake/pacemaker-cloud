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

sbin_PROGRAMS = cped

if HAVE_API_OPENSTACK
if HAVE_TRANSPORT_SSH2
sbin_PROGRAMS += cape-sshd-os
endif
if HAVE_TRANSPORT_MATAHARI
sbin_PROGRAMS += cape-mh-os
endif
endif

if HAVE_API_DELTACLOUD
if HAVE_TRANSPORT_SSH2
sbin_PROGRAMS += cape-sshd-dc
endif
if HAVE_TRANSPORT_MATAHARI
sbin_PROGRAMS += cape-mh-dc
endif
endif

noinst_HEADERS = config.h cpe_agent.h cpe_httpd.h mainloop.h \
	common_agent.h  config_loader.h  init-dbus.h  pcmk_pe.h \
	schema.xml org/pacemakercloud/QmfPackage.cpp \
	org/pacemakercloud/QmfPackage.h qmf_object.h \
	qmf_multiplexer.h qmf_job.h qmf_agent.h cpe_impl.h trans.h cape.h \
	matahari.h inst_ctrl.h

qmfauto_path = org/pacemakercloud
qmfauto_c = $(qmfauto_path)/QmfPackage.cpp

$(qmfauto_c): schema.xml
	qmf-gen -2 schema.xml

cped_SOURCES  = libinit.c mainloop.cpp $(qmfauto_c) common_agent.cpp \
	        cpe_agent.cpp cpe_impl.cpp cpe_httpd.cpp cpe_agent_register.cpp

cped_CPPFLAGS = $(libqb_CFLAGS) $(dbus_glib_1_CFLAGS) $(glib_CFLAGS) \
	$(qmf_CFLAGS) -I$(qmfauto_path) $(libxml2_CFLAGS)

cped_LDFLAGS  = $(libqb_LIBS) $(dbus_glib_1_LIBS) $(qmf_LIBS) $(glib_LIBS) \
		$(libmicrohttpd_LIBS) $(libcurl_LIBS) $(libxml2_LIBS)

cape_sshd_os_SOURCES = caped.c capeadmin.c recover.c cape.c trans_ssh.c \
	 pcmk_pe.c inst_ctrl.c openstack.c

cape_sshd_os_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(libxml2_CFLAGS) \
	$(pcmk_CFLAGS) $(libxslt_CFLAGS) $(uuid_LIBS) $(libcurl_CFLAGS) \
	$(libssh2_CFLAGS)

cape_sshd_os_LDFLAGS  = $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libcurl_LIBS) \
	$(libssh2_LIBS)

cape_mh_os_SOURCES  = caped.c capeadmin.c pcmk_pe.c recover.c cape.c \
	matahari.cpp inst_ctrl.c openstack.c config_loader.cpp \
	qmf_multiplexer.cpp qmf_object.cpp qmf_agent.cpp

cape_mh_os_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(qmf_CFLAGS) \
	$(libxml2_CFLAGS) -I$(qmfauto_path) $(pcmk_CFLAGS) $(libxslt_CFLAGS) \
	$(uuid_CFLAGS) $(libcurl_CFLAGS)

cape_mh_os_LDFLAGS  = $(libqb_LIBS) $(qmf_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libcurl_LIBS)

cape_sshd_dc_SOURCES = caped.c capeadmin.c recover.c cape.c trans_ssh.c \
	 pcmk_pe.c inst_ctrl.c deltacloud.c

cape_sshd_dc_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(libxml2_CFLAGS) \
	$(pcmk_CFLAGS) $(libxslt_CFLAGS) $(uuid_LIBS) $(libcurl_CFLAGS) \
	$(libssh2_CFLAGS) $(libdeltacloud_CFLAGS)

cape_sshd_dc_LDFLAGS  = $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libcurl_LIBS) \
	$(libssh2_LIBS) $(libdeltacloud_LIBS)

cape_mh_dc_SOURCES  = caped.c capeadmin.c pcmk_pe.c recover.c cape.c \
	matahari.cpp inst_ctrl.c deltacloud.c config_loader.cpp \
	qmf_multiplexer.cpp qmf_object.cpp qmf_agent.cpp

cape_mh_dc_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(qmf_CFLAGS) \
	$(libxml2_CFLAGS) -I$(qmfauto_path) $(pcmk_CFLAGS) $(libxslt_CFLAGS) \
	$(uuid_CFLAGS) $(libdeltacloud_CFLAGS)

cape_mh_dc_LDFLAGS  = $(libqb_LIBS) $(qmf_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libdeltacloud_LIBS)

sloccount:
	@echo -n "cape SSH OpenStack Monitor:      "
	@sloccount $(cape_sshd_os_SOURCES) | grep Physical | cut --delimiter== --fields=2
	@echo -n "cape Matahari OpenStack Monitor: "
	@@sloccount $(cape_mh_os_SOURCES) | grep Physical | cut --delimiter== --fields=2
	@echo -n "cape SSH Deltacloud Monitor:      "
	@sloccount $(cape_sshd_os_SOURCES) | grep Physical | cut --delimiter== --fields=2
	@echo -n "cape Matahari Deltacloud Monitor: "
	@@sloccount $(cape_mh_os_SOURCES) | grep Physical | cut --delimiter== --fields=2
	@echo -n "cpe:                   "
	@sloccount $(cped_SOURCES) | grep Physical | cut --delimiter== --fields=2
	@cd pcloudsh; make -f Makefile sloccount
