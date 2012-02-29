# Copyright (C) 2012 Red Hat, Inc.
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

noinst_PROGRAMS = cpe-tool2 pe_test

cpe_tool2_SOURCES = cpe-tool2.c ../src/libinit.c
cpe_tool2_CPPFLAGS = $(dbus_glib_1_CFLAGS)
cpe_tool2_LDFLAGS = $(dbus_glib_1_LIBS) $(libqb_LIBS)

pe_test_SOURCES = pe_test.c
pe_test_CPPFLAGS = -Wall $(glib_CFLAGS) $(libxml2_CFLAGS) $(pcmk_CFLAGS)
pe_test_LDADD = $(pcmk_LIBS) $(glib_LIBS) $(libxml2_LIBS)


if HAVE_CHECK

TESTS = recover.test basic.test escalation.test
check_PROGRAMS = recover.test basic.test escalation.test

recover_test_SOURCES = check_recover.c ../src/recover.c
recover_test_CPPFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/src $(libqb_CFLAGS) \
			$(glib_CFLAGS) $(libxml2_CFLAGS)
recover_test_LDADD = @CHECK_LIBS@ $(libqb_LIBS)

basic_test_SOURCES = check_basic.c ../src/pcmk_pe.c ../src/recover.c ../src/cape.c \
		     ../src/capeadmin.c
basic_test_CPPFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/src $(libqb_CFLAGS) \
			     $(glib_CFLAGS) $(libxml2_CFLAGS)  $(pcmk_CFLAGS) \
			     $(libxslt_CFLAGS) $(uuid_CFLAGS)
basic_test_LDADD = @CHECK_LIBS@ $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
			  $(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS)

escalation_test_SOURCES = check_escalation.c ../src/pcmk_pe.c ../src/recover.c \
			  ../src/cape.c ../src/capeadmin.c
escalation_test_CPPFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/src $(libqb_CFLAGS) \
			     $(glib_CFLAGS) $(libxml2_CFLAGS)  $(pcmk_CFLAGS) \
			     $(libxslt_CFLAGS) $(uuid_CFLAGS)
escalation_test_LDADD = @CHECK_LIBS@ $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
			  $(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS)

endif

if HAVE_SIM_SCALE
noinst_PROGRAMS += sim-cape-recovery sim-cape-sshd-master sim-cape-sshd-dummy

sim_cape_recovery_SOURCES = ../src/caped.c ../src/capeadmin.c ../src/recover.c ../src/cape.c ../src/pcmk_pe.c sim_recovery.c

sim_cape_recovery_CPPFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/src $(libqb_CFLAGS) \
			     $(glib_CFLAGS) $(libxml2_CFLAGS)  $(pcmk_CFLAGS) \
			     $(libxslt_CFLAGS) $(uuid_CFLAGS)

sim_cape_recovery_LDADD = @CHECK_LIBS@ $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
			  $(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS)


sim_cape_sshd_master_SOURCES = ../src/caped.c ../src/capeadmin.c ../src/recover.c ../src/cape.c ../src/trans_ssh.c ../src/pcmk_pe.c sim_deltacloud_master.c

sim_cape_sshd_master_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(libxml2_CFLAGS) \
	$(pcmk_CFLAGS) $(libxslt_CFLAGS) $(uuid_LIBS) $(libdeltacloud_LIBS) \
	$(libssh2_CFLAGS) -DSCALE_MASTER

sim_cape_sshd_master_LDFLAGS  = $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libdeltacloud_LIBS) \
	$(libssh2_LIBS)

sim_cape_sshd_dummy_SOURCES = ../src/caped.c ../src/capeadmin.c ../src/recover.c ../src/cape.c ../src/trans_ssh.c ../src/pcmk_pe.c sim_deltacloud_dummy.c

sim_cape_sshd_dummy_CPPFLAGS = $(libqb_CFLAGS) $(glib_CFLAGS) $(libxml2_CFLAGS) \
	$(pcmk_CFLAGS) $(libxslt_CFLAGS) $(uuid_LIBS) $(libdeltacloud_LIBS) \
	$(libssh2_CFLAGS) -DSCALE_DUMMY

sim_cape_sshd_dummy_LDFLAGS  = $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
	$(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS) $(libdeltacloud_LIBS) \
	$(libssh2_LIBS)
endif

clean-generic:
	$(AM_V_GEN)rm -f *.log
