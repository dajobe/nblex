#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.62])
AC_INIT([Non Blocking Lexer], [0.1], [http://bugs.librdf.org/],[nblex])
AC_CONFIG_SRCDIR([src/nblex.c])
AC_CONFIG_HEADERS([src/nblex_config.h])
AC_CONFIG_AUX_DIR(build)
AC_CONFIG_MACRO_DIR(build)
AM_INIT_AUTOMAKE([1.11 check-news std-options -Wobsolete -Wportability -Wsyntax -Wunsupported -Wextra-portability])

NBLEX_LIBTOOL_VERSION=0:0:0
AC_SUBST(NBLEX_LIBTOOL_VERSION)

# Checks for programs.
AM_PROG_AR
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB

m4_undefine([AC_PROG_CXX])
m4_defun([AC_PROG_CXX],[])
m4_undefine([AC_PROG_F77])
m4_defun([AC_PROG_F77],[])
LT_INIT

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([string.h stdlib.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
	src/nblex.h
 	src/Makefile])
AC_OUTPUT
