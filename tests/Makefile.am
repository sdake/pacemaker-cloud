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

if HAVE_CHECK

TESTS = basic.test
check_PROGRAMS = basic.test

basic_test_SOURCES = check_basic.c ../src/pcmk_pe.c ../src/cape.c
basic_test_CPPFLAGS = @CHECK_CFLAGS@ -I$(top_srcdir)/src $(libqb_CFLAGS) \
			     $(glib_CFLAGS) $(libxml2_CFLAGS)  $(pcmk_CFLAGS) \
			     $(libxslt_CFLAGS) $(uuid_CFLAGS)
basic_test_LDADD = @CHECK_LIBS@ $(libqb_LIBS) $(glib_LIBS) $(libxml2_LIBS) \
			  $(pcmk_LIBS) $(libxslt_LIBS) $(uuid_LIBS)

endif

clean-generic:
	$(AM_V_GEN)rm -f *.log
