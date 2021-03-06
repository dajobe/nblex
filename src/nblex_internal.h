/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_internal.h - internals
 *
 * Copyright (C) 2013-2014, David Beckett http://www.dajobe.org/
 *
 * This package is Free Software
 *
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 *
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 *
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 *
 */



#ifndef NBLEX_INTERNAL_H
#define NBLEX_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#define NBLEX_EXTERN_C extern "C"
#else
#define NBLEX_EXTERN_C
#endif

#ifdef NBLEX_INTERNAL

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define NBLEX_PRINTF_FORMAT(string_index, first_to_check_index) \
  __attribute__((__format__(__printf__, string_index, first_to_check_index)))
#else
#define NBLEX_PRINTF_FORMAT(string_index, first_to_check_index)
#endif

#ifdef __GNUC__
#define NBLEX_NORETURN __attribute__((noreturn))
#else
#define NBLEX_NORETURN
#endif

/* Can be over-ridden or undefined in a config.h file or -Ddefine */
#ifndef NBLEX_INLINE
#define NBLEX_INLINE inline
#endif

#define NBLEX_MALLOC(type, size) (type)malloc(size)
#define NBLEX_CALLOC(type, size, count) (type)calloc(size, count)
#define NBLEX_FREE(type, ptr)   free((void*)ptr)



struct nblex_world_s {
  /* opened flag */
  int opened;

  /* non-0 if lexing has started */
  int started;

  /* non-0 if data input has ended */
  int ended;

  /* Bytes byffer - bytes provided so far that have not yet formed a Unicode
   * Unicode codepoint.  This is always fixed at capacity 4 but can vary
   * in number of bytes stored, in @bytes_size.
   */
  unsigned char bytes[4];
  size_t bytes_size;

  /* Unicode codepoints buffer - codepoints so far that have not yet
   * formed a token. @codepoints_capacity is the size of this buffer
   * (in codepoints) and @codepoints_size the current use.
   */
  nblex_unichar* codepoints;
  size_t codepoints_capacity;
  size_t codepoints_size;
};


/* Safe casts: widening a value */
#define NBLEX_GOOD_CAST(t, v) (t)(v)

/* Unsafe casts: narrowing a value */
#define NBLEX_BAD_CAST(t, v) (t)(v)


/* nblex_unicode.c */
int nblex_unicode_utf8_string_put_char(nblex_unichar c, unsigned char *output, size_t length);
int nblex_unicode_utf8_string_get_char(const unsigned char *input, size_t length, nblex_unichar *output);
int nblex_unicode_is_xml11_namestartchar(nblex_unichar c);
int nblex_unicode_is_xml10_namestartchar(nblex_unichar c);
int nblex_unicode_is_namestartchar(nblex_unichar c);
int nblex_unicode_is_xml10_namechar(nblex_unichar c);
int nblex_unicode_is_xml11_namechar(nblex_unichar c);
int nblex_unicode_is_namechar(nblex_unichar c);


/* end of NBLEX_INTERNAL */
#endif


#ifdef __cplusplus
}
#endif

#endif
