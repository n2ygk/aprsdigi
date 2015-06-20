dnl Process this file with autoconf to produce a configure script.
AC_INIT([aprsdigi], [3.10.0])
AC_CONFIG_SRCDIR([aprsshm.h])
AM_INIT_AUTOMAKE
AC_PREFIX_DEFAULT(/usr)

dnl Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PATH_PROG(GROFF,groff,.:$PATH)

dnl Checks for libraries.
AC_CHECKING(for N2YGK's -lax25 extensions:)
AC_CHECK_LIB(ax25, parse_raw_ax25, AC_DEFINE(HAVE_LIBAX25_EXTENSIONS))
AC_CHECK_LIB(ax25, ax25_config_load_ports)

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(fcntl.h limits.h sys/ioctl.h sys/time.h unistd.h ax25/axconfig.h)

if test "$ac_cv_header_ax25_axconfig_h" = yes ; then
	CPPFLAGS="$CPPFLAGS -I/usr/include/ax25"
fi

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_STRUCT_TM
dnl following ip6 test lifted from iperf:
dnl ===================================================================
dnl Check for IPV6 support
dnl We have avoided checking for ss_family in sockaddr_storage because
dnl linux 2.4.x versions support IPv6 but do not have this structure
dnl Just check for the presence of sockaddr_storage, sockaddr_in6 
dnl and sin6_port
dnl ===================================================================
AC_CHECKING(for IPv6)
AC_TRY_COMPILE(
	[#include <sys/types.h>
    	 #include <sys/socket.h>
	 #include <netinet/in.h>
	 #include <netinet/tcp.h>
	],
      	[struct sockaddr_storage sa_union, *sa_unionp;
  	 struct sockaddr_in6 *sa;
  	 sa_unionp = &sa_union;
  	 sa = (struct sockaddr_in6 *)sa_unionp;
  	 sa->sin6_port = ntohs(5001);
        ],
    	ac_accept_ipv6=yes,
    	ac_accept_ipv6=no)
if test "$ac_accept_ipv6" = yes ; then
 	AC_DEFINE(IPV6)
fi	

dnl Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_TYPE_SIGNAL
AC_FUNC_WAIT3
AC_CHECK_FUNCS(select socket strdup strtol uname shmctl)

dnl Compile-time options.
AC_ARG_ENABLE(shm,[  --disable-shm           Do not use shared memory])
if test "$enable_shm" != no; then
  AC_DEFINE(USE_SHM)
fi
AC_ARG_WITH(ax25inc,[  --with-ax25inc=path     Location of ax25 include files if other than <ax25/>],CPPFLAGS="$CPPFLAGS -I$with_ax25inc")

AC_OUTPUT(Makefile)
AC_OUTPUT(aprsdigi.spec)
