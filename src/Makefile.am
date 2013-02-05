lib_LTLIBRARIES = libnblex.la

AM_CFLAGS= -DNBLEX_INTERNAL=1

nodist_pkginclude_HEADERS = nblex.h
noinst_HEADERS = nblex_internal.h

libnblex_la_LDFLAGS = -version-info @NBLEX_LIBTOOL_VERSION@
libnblex_la_SOURCES = \
nblex_core.c \
nblex_unicode.c


bin_PROGRAMS = nblex

nblex_SOURCES = \
nblex.c
nblex_LDADD = libnblex.la
