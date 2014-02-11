#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.62])
AC_INIT([Non Blocking Lexer], [0.1], [http://bugs.librdf.org/],[nblex])
AC_CONFIG_SRCDIR([src/nblex.c])
AC_CONFIG_HEADERS([src/nblex_config.h])
AC_CONFIG_AUX_DIR(build)
AC_CONFIG_MACRO_DIR(build)
AM_INIT_AUTOMAKE([1.11 check-news std-options -Wobsolete -Wportability -Wsyntax -Wunsupported -Wextra-portability])

AM_MAINTAINER_MODE

NBLEX_LIBTOOL_VERSION=0:0:0
AC_SUBST(NBLEX_LIBTOOL_VERSION)

# Checks for programs.
AM_PROG_AR
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_C_O
AC_MSG_CHECKING(whether $CC is clang)
CC_IS_CLANG=no
if $CC 2>&1 | grep clang >/dev/null 2>&1; then
  CC_IS_CLANG=yes
else
  :
fi
AC_MSG_RESULT($CC_IS_CLANG)
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
AC_DEFUN([NBLEX_CC_TRY_FLAG], [
  AC_MSG_CHECKING([whether $CC supports $1])
  nblex_save_CFLAGS="$CFLAGS"
  if test $CC_IS_CLANG = yes; then
    # Make unknown warnings into errors for clang so the test below works
    CFLAGS="-Werror $CFLAGS"
  fi
  CFLAGS="$CFLAGS $1"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([ ])], [nblex_cc_flag=yes], [nblex_cc_flag=no])
  CFLAGS="$nblex_save_CFLAGS"
  if test "X$nblex_cc_flag" = "Xyes"; then
    ifelse([$2], , :, [$2])
  else
    ifelse([$3], , :, [$3])
  fi
  AC_MSG_RESULT($nblex_cc_flag)
])

# GCC warning options
# http://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#
# Too noisy:
#  -Wconversion
#  -Wformat-nonliteral : variables for format strings
#  -Wmissing-field-initializers : too noisy in raptor_rss_common.c
#  -Wsystem-headers   : not debugging system
#  -Wsign-conversion: : many int / unsigned int / size_t
#  -Wunused-parameter : variables can be marked  __attribute__('unused')
#
# Apple gcc specific (probably): -Wshorten-64-to-32
possible_warnings="\
-Wall \
-Wc++-compat \
-Wextra \
-Wunused \
\
-Waggregate-return \
-Wbad-function-cast \
-Wcast-align \
-Wdeclaration-after-statement \
-Wdisabled-optimization \
-Wdiv-by-zero \
-Wendif-labels \
-Werror-implicit-function-declaration \
-Wfloat-equal \
-Wformat-security \
-Wframe-larger-than=4096 \
-Winit-self \
-Winline \
-Wmissing-declarations \
-Wmissing-format-attribute \
-Wmissing-noreturn \
-Wmissing-prototypes \
-Wnested-externs \
-Wold-style-definition \
-Wpacked \
-Wpointer-arith \
-Wredundant-decls \
-Wshadow \
-Wsign-compare \
-Wstrict-prototypes \
-Wswitch-enum \
-Wundef \
-Wunsafe-loop-optimizations \
-Wwrite-strings \
\
-Wshorten-64-to-32
\
-Wno-conversion \
-Wno-format-nonliteral \
-Wno-missing-field-initializers \
-Wno-sign-conversion \
-Wno-system-headers \
-Wno-unused-parameter \
"

warning_cflags=
if test "$USE_MAINTAINER_MODE" = yes; then
  AC_MSG_CHECKING(for supported $CC warning flags)
  AC_MSG_RESULT($warning_cflags)
  for warning in $possible_warnings; do
    NBLEX_CC_TRY_FLAG([$warning], [warning_cflags="$warning_cflags $warning"])
  done
  AC_MSG_CHECKING($CC supports warning flags)
  AC_MSG_RESULT($warning_cflags)
fi

MAINTAINER_CPPFLAGS="$warning_cflags"


if test "$USE_MAINTAINER_MODE" = yes; then
  AC_DEFINE([MAINTAINER_MODE], [1], [Define to 1 if maintainer mode is enabled.])
  CPPFLAGS="$MAINTAINER_CPPFLAGS $CPPFLAGS"
fi

# Checks for library functions.

AC_CONFIG_FILES([Makefile
	src/nblex.h
 	src/Makefile])
AC_OUTPUT
