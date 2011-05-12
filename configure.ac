#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT([pacemaker-cloud], [0.1.0], [pacemaker-cloud@oss.clusterlabs.org])
AM_INIT_AUTOMAKE([-Wno-portability])
AC_CONFIG_SRCDIR([src/dpe_agent.cpp])
AC_CONFIG_HEADERS([src/config.h])

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
PKG_CHECK_MODULES(libqb, libqb)

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

AC_CHECK_HEADERS(rpc/netdb.h tirpc/netconfig.h)
if test "x${ac_cv_header_rpc_netdb_h}" != "xyes" ; then
	if test "x${ac_cv_header_tirpc_netconfig_h}" != "xyes" ; then
		AC_MSG_FAILURE(Please install libtirpc-devel)
	fi
	# hack until heartbeat can handle tirpc
	mkdir src/rpc
	touch src/rpc/netdb.h
fi

AC_ARG_WITH([systemd],
	    [AS_HELP_STRING([--with-systemd],
			    [compile systemd support])],
	    [AC_DEFINE([WITH_SYSTEMD], 1, [with systemd])]
	    [])

AC_CHECK_HEADERS(pacemaker/crm_config.h)
if test "x${ac_cv_header_pacemaker_crm_config_h}" != "xyes"; then
   AC_MSG_FAILURE(Please install pacemaker-devel)
fi

AC_CHECK_HEADERS(heartbeat/glue_config.h)
if test "x${ac_cv_header_heartbeat_glue_config_h}" != "xyes"; then
   AC_MSG_FAILURE(Please install heartbeat-devel)
fi

AC_CHECK_HEADERS(clplumbing/lsb_exitcodes.h)
if test "x${ac_cv_header_clplumbing_lsb_exitcodes_h}" != "xyes"; then
   AC_MSG_FAILURE(Please install cluster-glue-libs-devel)
fi

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

AC_ARG_WITH([qmf-lib-path],
	    [AS_HELP_STRING([--with-qpid-lib-path],
			    [location of the QPID and QMF libraries])],
	    [qmf_LIBS="-L$withval -lqmf -lqmf2 -lqpidtypes -lqpidcommon -lqpidmessaging"],
	               [qmf_LIBS="-lqmf -lqmf2 -lqpidtypes -lqpidcommon -lqpidmessaging"])
AC_SUBST([qmf_LIBS])

AC_CONFIG_FILES([Makefile src/Makefile])

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
AC_MSG_RESULT([  dbus_glib_1_CFLAGS        = ${dbus_glib_1_CFLAGS}])
AC_MSG_RESULT([  dbus_glib_1_LIBS          = ${dbus_glib_1_LIBS}])
AC_MSG_RESULT([  qmf_CFLAGS                = ${qmf_CFLAGS}])
AC_MSG_RESULT([  qmf_LIBS                  = ${qmf_LIBS}])
AC_MSG_RESULT([  pcmk_CFLAGS               = ${pcmk_CFLAGS}])
AC_MSG_RESULT([  pcmk_LIBS                 = ${pcmk_LIBS}])
AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        CXXFLAGS    = ${CXXFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
