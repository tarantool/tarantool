/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file contains routines used to translate between UTF-8.
 *
 * Notes on UTF-8:
 *
 *   Byte-0    Byte-1    Byte-2    Byte-3    Value
 *  0xxxxxxx                                 00000000 00000000 0xxxxxxx
 *  110yyyyy  10xxxxxx                       00000000 00000yyy yyxxxxxx
 *  1110zzzz  10yyyyyy  10xxxxxx             00000000 zzzzyyyy yyxxxxxx
 *  11110uuu  10uuzzzz  10yyyyyy  10xxxxxx   000uuuuu zzzzyyyy yyxxxxxx
 *
 *
 */
#include "sqliteInt.h"
#include <assert.h>
#include "vdbeInt.h"

#if !defined(SQLITE_AMALGAMATION) && SQLITE_BYTEORDER==0
/*
 * The following constant value is used by the SQLITE_BIGENDIAN and
 * SQLITE_LITTLEENDIAN macros.
 */
const int sqlite3one = 1;
#endif				/* SQLITE_AMALGAMATION && SQLITE_BYTEORDER==0 */

/*
 * This lookup table is used to help decode the first byte of
 * a multi-byte UTF8 character.
 */
static const unsigned char sqlite3Utf8Trans1[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
};

#define WRITE_UTF8(zOut, c) {                          \
  if( c<0x00080 ){                                     \
    *zOut++ = (u8)(c&0xFF);                            \
  }                                                    \
  else if( c<0x00800 ){                                \
    *zOut++ = 0xC0 + (u8)((c>>6)&0x1F);                \
    *zOut++ = 0x80 + (u8)(c & 0x3F);                   \
  }                                                    \
  else if( c<0x10000 ){                                \
    *zOut++ = 0xE0 + (u8)((c>>12)&0x0F);               \
    *zOut++ = 0x80 + (u8)((c>>6) & 0x3F);              \
    *zOut++ = 0x80 + (u8)(c & 0x3F);                   \
  }else{                                               \
    *zOut++ = 0xF0 + (u8)((c>>18) & 0x07);             \
    *zOut++ = 0x80 + (u8)((c>>12) & 0x3F);             \
    *zOut++ = 0x80 + (u8)((c>>6) & 0x3F);              \
    *zOut++ = 0x80 + (u8)(c & 0x3F);                   \
  }                                                    \
}

/*
 * Translate a single UTF-8 character.  Return the unicode value.
 *
 * During translation, assume that the byte that zTerm points
 * is a 0x00.
 *
 * Write a pointer to the next unread byte back into *pzNext.
 *
 * Notes On Invalid UTF-8:
 *
 *  *  This routine never allows a 7-bit character (0x00 through 0x7f) to
 *     be encoded as a multi-byte character.  Any multi-byte character that
 *     attempts to encode a value between 0x00 and 0x7f is rendered as 0xfffd.
 *
 *  *  This routine never allows a UTF16 surrogate value to be encoded.
 *     If a multi-byte character attempts to encode a value between
 *     0xd800 and 0xe000 then it is rendered as 0xfffd.
 *
 *  *  Bytes in the range of 0x80 through 0xbf which occur as the first
 *     byte of a character are interpreted as single-byte characters
 *     and rendered as themselves even though they are technically
 *     invalid characters.
 *
 *  *  This routine accepts over-length UTF8 encodings
 *     for unicode values 0x80 and greater.  It does not change over-length
 *     encodings to 0xfffd as some systems recommend.
 */
#define READ_UTF8(zIn, zTerm, c)                           \
  c = *(zIn++);                                            \
  if( c>=0xc0 ){                                           \
    c = sqlite3Utf8Trans1[c-0xc0];                         \
    while( zIn!=zTerm && (*zIn & 0xc0)==0x80 ){            \
      c = (c<<6) + (0x3f & *(zIn++));                      \
    }                                                      \
    if( c<0x80                                             \
        || (c&0xFFFFF800)==0xD800                          \
        || (c&0xFFFFFFFE)==0xFFFE ){  c = 0xFFFD; }        \
  }
u32
sqlite3Utf8Read(const unsigned char **pz	/* Pointer to string from which to read char */
    )
{
	unsigned int c;

	/* Same as READ_UTF8() above but without the zTerm parameter.
	 * For this routine, we assume the UTF8 string is always zero-terminated.
	 */
	c = *((*pz)++);
	if (c >= 0xc0) {
		c = sqlite3Utf8Trans1[c - 0xc0];
		while ((*(*pz) & 0xc0) == 0x80) {
			c = (c << 6) + (0x3f & *((*pz)++));
		}
		if (c < 0x80
		    || (c & 0xFFFFF800) == 0xD800
		    || (c & 0xFFFFFFFE) == 0xFFFE) {
			c = 0xFFFD;
		}
	}
	return c;
}

/*
 * If the TRANSLATE_TRACE macro is defined, the value of each Mem is
 * printed on stderr on the way into and out of sqlite3VdbeMemTranslate().
 */
/* #define TRANSLATE_TRACE 1 */

/*
 * pZ is a UTF-8 encoded unicode string. If nByte is less than zero,
 * return the number of unicode characters in pZ up to (but not including)
 * the first 0x00 byte. If nByte is not less than zero, return the
 * number of unicode characters in the first nByte of pZ (or up to
 * the first 0x00, whichever comes first).
 */
int
sqlite3Utf8CharLen(const char *zIn, int nByte)
{
	int r = 0;
	const u8 *z = (const u8 *)zIn;
	const u8 *zTerm;
	if (nByte >= 0) {
		zTerm = &z[nByte];
	} else {
		zTerm = (const u8 *)(-1);
	}
	assert(z <= zTerm);
	while (*z != 0 && z < zTerm) {
		SQLITE_SKIP_UTF8(z);
		r++;
	}
	return r;
}

/* This test function is not currently used by the automated test-suite.
 * Hence it is only available in debug builds.
 */
#if defined(SQLITE_TEST) && defined(SQLITE_DEBUG)
/*
 * Translate UTF-8 to UTF-8.
 *
 * This has the effect of making sure that the string is well-formed
 * UTF-8.  Miscoded characters are removed.
 *
 * The translation is done in-place and aborted if the output
 * overruns the input.
 */
int
sqlite3Utf8To8(unsigned char *zIn)
{
	unsigned char *zOut = zIn;
	unsigned char *zStart = zIn;
	u32 c;

	while (zIn[0] && zOut <= zIn) {
		c = sqlite3Utf8Read((const u8 **)&zIn);
		if (c != 0xfffd) {
			WRITE_UTF8(zOut, c);
		}
	}
	*zOut = 0;
	return (int)(zOut - zStart);
}
#endif

