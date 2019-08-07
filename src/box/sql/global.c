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
 *
 * This file contains definitions of global variables and constants.
 */
#include "sqlInt.h"

/* An array to map all upper-case characters into their corresponding
 * lower-case character.
 *
 * sql only considers US-ASCII (or EBCDIC) characters.  We do not
 * handle case conversions for the UTF character set since the tables
 * involved are nearly as big or bigger than sql itself.
 */
const unsigned char sqlUpperToLower[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
	36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
	54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 97, 98, 99, 100, 101, 102,
	    103,
	104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117,
	    118, 119, 120, 121,
	122, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105,
	    106, 107,
	108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
	    122, 123, 124, 125,
	126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
	    140, 141, 142, 143,
	144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157,
	    158, 159, 160, 161,
	162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
	    176, 177, 178, 179,
	180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193,
	    194, 195, 196, 197,
	198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211,
	    212, 213, 214, 215,
	216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229,
	    230, 231, 232, 233,
	234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247,
	    248, 249, 250, 251,
	252, 253, 254, 255
};

/*
 * The following 256 byte lookup table is used to support sqls built-in
 * equivalents to the following standard library functions:
 *
 *   isspace()                        0x01
 *   isalpha()                        0x02
 *   isdigit()                        0x04
 *   isalnum()                        0x06
 *   isxdigit()                       0x08
 *   toupper()                        0x20
 *   sql identifier character      0x40
 *   Quote character                  0x80
 *
 * Bit 0x20 is set if the mapped character requires translation to upper
 * case. i.e. if the character is a lower-case ASCII character.
 * If x is a lower-case ASCII character, then its upper-case equivalent
 * is (x - 0x20). Therefore toupper() can be implemented as:
 *
 *   (x & ~(map[x]&0x20))
 *
 * The equivalent of tolower() is implemented using the sqlUpperToLower[]
 * array. tolower() is used more often than toupper() by sql.
 *
 * Bit 0x40 is set if the character is non-alphanumeric and can be used in an
 * sql identifier.  Identifiers are alphanumerics, "_", "$", and any
 * non-ASCII UTF character. Hence the test for whether or not a character is
 * part of an identifier is 0x46.
 */
const unsigned char sqlCtypeMap[256] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 00..07    ........ */
	0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,	/* 08..0f    ........ */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 10..17    ........ */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 18..1f    ........ */
	0x01, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x80,	/* 20..27     !"#$%&' */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 28..2f    ()*+,-./ */
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,	/* 30..37    01234567 */
	0x0c, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 38..3f    89:;<=>? */

	0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x02,	/* 40..47    @ABCDEFG */
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,	/* 48..4f    HIJKLMNO */
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,	/* 50..57    PQRSTUVW */
	0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x40,	/* 58..5f    XYZ[\]^_ */
	0x00, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x22,	/* 60..67    `abcdefg */
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,	/* 68..6f    hijklmno */
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,	/* 70..77    pqrstuvw */
	0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 78..7f    xyz{|}~. */

	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* 80..87    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* 88..8f    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* 90..97    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* 98..9f    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* a0..a7    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* a8..af    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* b0..b7    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* b8..bf    ........ */

	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* c0..c7    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* c8..cf    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* d0..d7    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* d8..df    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* e0..e7    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* e8..ef    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,	/* f0..f7    ........ */
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40	/* f8..ff    ........ */
};

/* The minimum PMA size is set to this value multiplied by the database
 * page size in bytes.
 */
#ifndef SQL_SORTER_PMASZ
#define SQL_SORTER_PMASZ 250
#endif

/*
 * The following singleton contains the global configuration for
 * the sql library.
 */
SQL_WSD struct sqlConfig sqlConfig = {
	SQL_DEFAULT_MMAP_SIZE,	/* szMmap */
	SQL_MAX_MMAP_SIZE,	/* mxMmap */
	SQL_SORTER_PMASZ,	/* szPma */
	/* All the rest should always be initialized to zero */
	0,			/* isInit */
	0,			/* inProgress */
#ifdef SQL_VDBE_COVERAGE
	0,			/* xVdbeBranch */
	0,			/* pVbeBranchArg */
#endif
	0x7ffffffe		/* iOnceResetThreshold */
};

/*
 * The value of the "pending" byte must be 0x40000000 (1 byte past the
 * 1-gibabyte boundary) in a compatible database.  sql never uses
 * the database page that contains the pending byte.  It never attempts
 * to read or write that page.  The pending byte page is set aside
 * for use by the VFS layers as space for managing file locks.
 *
 * During testing, it is often desirable to move the pending byte to
 * a different position in the file.  This allows code that has to
 * deal with the pending byte to run on files that are much smaller
 * than 1 GiB.
 *
 * IMPORTANT:  Changing the pending byte to any value other than
 * 0x40000000 results in an incompatible database file format!
 * Changing the pending byte during operation will result in undefined
 * and incorrect behavior.
 */
int sqlPendingByte = 0x40000000;

#include "opcodes.h"
/*
 * Properties of opcodes.  The OPFLG_INITIALIZER macro is
 * created by mkopcodeh.awk during compilation.  Data is obtained
 * from the comments following the "case OP_xxxx:" statements in
 * the vdbe.c file.
 */
const unsigned char sqlOpcodeProperty[] = OPFLG_INITIALIZER;

