# Copyright (C) 2010-2011 Red Hat, Inc.
#
# Authors:
#	Angus Salkeld <asalkeld@redhat.com>
#	Steven Dake <sdake@redhat.com>
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

SPEC	= $(PACKAGE_NAME).spec

TARFILE = $(PACKAGE_NAME)-$(VERSION).tar.gz

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure depcomp \
			  config.guess config.sub missing install-sh \
			  autoheader automake autoconf

EXTRA_DIST		= autogen.sh COPYING pacemaker-cloud.spec.in

dist_doc_DATA		= COPYING

SUBDIRS = init man src src/pcloudsh

$(TARFILE):
	$(MAKE) dist

coverity:
	cov-build --dir=cov make
	cov-analyze --dir cov --concurrency --wait-for-license
	cov-format-errors --dir cov

$(SPEC): $(SPEC).in
        DATE=$(shell date "+%a %b %d %Y")
	sed \
		-e "s#@version@#$(VERSION)#g" \
		-e "s#@date@#$(DATE)#g" \
	$< > $@;

RPMBUILDOPTS    = --define "_sourcedir $(abs_builddir)" \
	--define "_specdir $(abs_builddir)" \
	--define "_builddir $(abs_builddir)" \
	--define "_srcrpmdir $(abs_builddir)" \
	--define "_rpmdir $(abs_builddir)"

srpm: clean
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(RPMBUILDOPTS) --nodeps -bs $(SPEC)

rpm: clean
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(RPMBUILDOPTS) -ba $(SPEC)

.PHONY: coverity srpm rpm
