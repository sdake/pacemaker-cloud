#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.66])
AC_INIT([Cloud Policy Engine], [0.1], [asalkeld@redhat.com])
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
PKG_CHECK_MODULES(qpid, qpid >= 0.9)
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

AC_CONFIG_FILES([Makefile src/Makefile])

AC_ARG_WITH([systemd],
	    [  --with-systemd          compile systemd support ],
	    [ default=no ])

if test "x${with_systemd}" = xyes ; then
	AC_DEFINE([WITH_SYSTEMD], 1, [with systemd])
fi

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
AC_MSG_RESULT([$PACKAGE build info:])
AC_MSG_RESULT([  Final        CFLAGS      = ${CFLAGS}])
AC_MSG_RESULT([  Final        CPPFLAGS    = ${CPPFLAGS}])
AC_MSG_RESULT([  Final        CXXFLAGS    = ${CXXFLAGS}])
AC_MSG_RESULT([  Final        LDFLAGS     = ${LDFLAGS}])
