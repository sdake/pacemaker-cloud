#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT([pacemaker-cloud], [0.6.0], [pcmk-cloud@oss.clusterlabs.org])
AM_INIT_AUTOMAKE([-Wno-portability color-tests parallel-tests])
AC_CONFIG_SRCDIR([src/caped.c])
AC_CONFIG_HEADERS([src/config.h])

AM_PATH_PYTHON
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_MAKE_SET
AC_CHECK_PROGS([PKGCONFIG], [pkg-config])

dnl Fix default variables - "prefix" variable if not specified
if test "$prefix" = "NONE"; then
	prefix="/usr"
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi


# Checks for libraries.
PKG_CHECK_MODULES(glib, glib-2.0 >= 2.0)
PKG_CHECK_MODULES(dbus_glib_1, dbus-glib-1 >= 0.25)
PKG_CHECK_MODULES(libxml2, libxml-2.0 >= 2.7.7)
PKG_CHECK_MODULES(libxslt, libxslt >= 0.25)
PKG_CHECK_MODULES(libqb, libqb)
PKG_CHECK_MODULES(uuid, uuid)
PKG_CHECK_MODULES(libmicrohttpd, libmicrohttpd)
PKG_CHECK_MODULES(libcurl, libcurl)
PKG_CHECK_MODULES(libdeltacloud, libdeltacloud)
PKG_CHECK_MODULES(libssh2, libssh2)
PKG_CHECK_MODULES([CHECK], [check >= 0.9.4],[with_check=yes],[with_check=no])
AM_CONDITIONAL(HAVE_CHECK, test "${with_check}" = "yes")

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([select strdup strerror])

AC_CHECK_HEADERS(rpc/netdb.h tirpc/netconfig.h sys/un.h sys/stat.h fcntl.h \
		 sys/socket.h)
if test "x${ac_cv_header_rpc_netdb_h}" != "xyes" ; then
	if test "x${ac_cv_header_tirpc_netconfig_h}" != "xyes" ; then
		AC_MSG_FAILURE(Please install libtirpc-devel)
	fi
	# hack until heartbeat can handle tirpc
	mkdir src/rpc
	touch src/rpc/netdb.h
fi

fail=0

  AC_CHECK_HEADER([pacemaker/crm_config.h],,
    [fail=1; AC_MSG_WARN([Please install pacemaker-libs-devel])])

  AC_CHECK_HEADER([heartbeat/glue_config.h],,
    [fail=1; AC_MSG_FAILURE([Please install cluster-glue-libs-devel])])

  AC_CHECK_HEADER([clplumbing/lsb_exitcodes.h],,
    [fail=1; AC_MSG_FAILURE([Please install cluster-glue-libs-devel])])

test $fail = 1 && AC_MSG_ERROR([missing packages; see above])

pcmk_CFLAGS="-I/usr/include/pacemaker -I/usr/include/heartbeat"
AC_SUBST([pcmk_CFLAGS])

pcmk_LIBS="-lcrmcommon -lpe_status -lpengine -lcib -ltransitioner -lplumb"
AC_SUBST([pcmk_LIBS])

AC_ARG_WITH([qmf-include-path],
	    [AS_HELP_STRING([--with-qmf-include-path],
			    [location of the QPID and QMF headers, defaults to system path])],
	    [qmf_CFLAGS="-I$withval"],
	    [qmf_CFLAGS=""])
AC_SUBST([qmf_CFLAGS])

qmf_LIBS=

AC_ARG_WITH([qmf-lib-path],
	    [AS_HELP_STRING([--with-qmf-lib-path],
			    [location of the QPID and QMF libraries])],
	    [qmf_LIBS="-L$withval"])

fail=0
AC_SUBST([qmf_LIBS])

AC_ARG_ENABLE([debug],
	[  --enable-debug			: enable debug build. ],
	[ default="no" ])

AC_ARG_ENABLE([sim-scale],
	[ --enable-sim-scale			| enable sim scale binaries. ],
	[ defualt="no" ])

AC_LANG_PUSH(C++)
  cpe_saved_libs=$LIBS

  cpe_lib=qpidcommon
  LIBS="$LIBS -l$cpe_lib"
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [#include <qpid/sys/Time.h>
       using qpid::sys::AbsTime;],
      [AbsTime t = AbsTime::now();])],
    [qmf_LIBS="$qmf_LIBS -l$cpe_lib"],
    [fail=1; AC_MSG_WARN([lib$cpe_lib is not installed.])])

  cpe_lib=qpidmessaging
  LIBS="$LIBS -l$cpe_lib"
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [#include <qpid/messaging/Connection.h>],
      [qpid::messaging::Connection c("", "");])],
    [qmf_LIBS="$qmf_LIBS -l$cpe_lib"],
    [fail=1; AC_MSG_WARN([lib$cpe_lib is not installed.])])

  cpe_lib=qpidtypes
  LIBS="$LIBS -l$cpe_lib"
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [#include <qpid/types/Variant.h>],
      [qpid::types::Variant v;])],
    [qmf_LIBS="$qmf_LIBS -l$cpe_lib"],
    [fail=1; AC_MSG_WARN([lib$cpe_lib is not installed.])])

  cpe_lib=qmf
  LIBS="$LIBS -l$cpe_lib"
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [#include <qpid/agent/ManagementAgent.h>
       using qpid::management::ManagementAgent;],
      [ManagementAgent::Singleton* singleton;
       ManagementAgent* agent = singleton->getInstance();])],
    [qmf_LIBS="$qmf_LIBS -l$cpe_lib"],
    [fail=1; AC_MSG_WARN([lib$cpe_lib is not installed.])])

  # FIXME do something similar for qmf2; but for now, just use it unconditionally.
  # If everything else is here, odds are very good that -lqmf2 will work, too.
  if test $fail = 0; then
    cpe_lib=qmf2
    qmf_LIBS="$qmf_LIBS -l$cpe_lib"
  fi

  LIBS=$cpe_saved_libs
AC_LANG_POP([C++])
test $fail = 1 && AC_MSG_ERROR([missing libraries; see above])

if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Enable debug version])
	OPT_CFLAGS="-O0"
	OPT_CXXFLAGS="-O0"
else
	OPT_CFLAGS="-O2"
	OPT_CXXFLAGS="-O2"
fi

if test "x${enable_sim_scale}" = xyes ; then
	AC_DEFINE([HAVE_SIM_SCALE], 1,[have sim scale bins])
	AC_MSG_NOTICE([Enabling Scale Binaries])
fi
AM_CONDITIONAL(HAVE_SIM_SCALE, [test "x${enable_sim_scale}" = xyes])

# gdb flags
if test "x${GCC}" = xyes; then
	GDB_FLAGS="-ggdb3"
else
	GDB_FLAGS="-g"
fi

CFLAGS="$ENV_CFLAGS $OPT_CFLAGS $GDB_FLAGS"

CXXFLAGS="$ENV_CPPFLAGS $OPT_CXXFLAGS $GDB_FLAGS"

AC_CONFIG_FILES([Makefile
		 init/Makefile
		 man/Makefile
		 tests/Makefile
		 src/Makefile
		 src/pcloudsh/Makefile
		 src/pcloudsh/pcmkconfig.py])

AC_OUTPUT

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Man pages                = ${mandir}])
AC_MSG_RESULT([  Doc dir                  = ${docdir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  Arch-independent files   = ${datadir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([])
AC_MSG_RESULT([Dependancies:])
AC_MSG_RESULT([  libqb_CFLAGS              = ${libqb_CFLAGS}])
AC_MSG_RESULT([  libqb_LIBS                = ${libqb_LIBS}])
AC_MSG_RESULT([  glib_CFLAGS               = ${glib_CFLAGS}])
AC_MSG_RESULT([  glib_LIBS                 = ${glib_LIBS}])
AC_MSG_RESULT([  libxml2_CFLAGS            = ${libxml2_CFLAGS}])
AC_MSG_RESULT([  libxml2_LIBS              = ${libxml2_LIBS}])
AC_MSG_RESULT([  libxslt_CFLAGS            = ${libxslt_CFLAGS}])
AC_MSG_RESULT([  libxslt_LIBS              = ${libxslt_LIBS}])
AC_MSG_RESULT([  dbus_glib_1_CFLAGS        = ${dbus_glib_1_CFLAGS}])
AC_MSG_RESULT([  dbus_glib_1_LIBS          = ${dbus_glib_1_LIBS}])
AC_MSG_RESULT([  qmf_CFLAGS                = ${qmf_CFLAGS}])
AC_MSG_RESULT([  qmf_LIBS                  = ${qmf_LIBS}])
AC_MSG_RESULT([  pcmk_CFLAGS               = ${pcmk_CFLAGS}])
AC_MSG_RESULT([  pcmk_LIBS                 = ${pcmk_LIBS}])
AC_MSG_RESULT([  uuid_CFLAGS               = ${uuid_CFLAGS}])
AC_MSG_RESULT([  uuid_LIBS                 = ${uuid_LIBS}])
AC_MSG_RESULT([  libmicrohttpd_CFLAGS      = ${libmicrohttpd_CFLAGS}])
AC_MSG_RESULT([  libmicrohttpd_LIBS        = ${libmicrohttpd_LIBS}])
AC_MSG_RESULT([  libcurl_CFLAGS            = ${libcurl_CFLAGS}])
AC_MSG_RESULT([  libcurl_LIBS              = ${libcurl_LIBS}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        CXXFLAGS    = ${CXXFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
