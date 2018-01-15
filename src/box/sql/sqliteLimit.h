#ifndef TARANTOOL_SQL_SQLITELIMIT_H_INCLUDED
#define TARANTOOL_SQL_SQLITELIMIT_H_INCLUDED
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
 * This file defines various limits of what SQLite can process.
 */
#include "trivia/util.h"

enum {
	/*
	 * The maximum value of a ?nnn wildcard that the parser will accept.
	 */
	SQL_BIND_PARAMETER_MAX = 65000,
};

/*
 * The maximum length of a TEXT or BLOB in bytes.   This also
 * limits the size of a row in a table or index.
 *
 * The hard limit is the ability of a 32-bit signed integer
 * to count the size: 2^31-1 or 2147483647.
 */
#ifndef SQLITE_MAX_LENGTH
#define SQLITE_MAX_LENGTH 1000000000
#endif

/*
 * This is the maximum number of
 *
 *    * Columns in a table
 *    * Columns in an index
 *    * Columns in a view
 *    * Terms in the SET clause of an UPDATE statement
 *    * Terms in the result set of a SELECT statement
 *    * Terms in the GROUP BY or ORDER BY clauses of a SELECT statement.
 *    * Terms in the VALUES clause of an INSERT statement
 *
 * The hard upper limit here is 32676.  Most database people will
 * tell you that in a well-normalized database, you usually should
 * not have more than a dozen or so columns in any table.  And if
 * that is the case, there is no point in having more than a few
 * dozen values in any of the other situations described above.
 */
#ifndef SQLITE_MAX_COLUMN
#define SQLITE_MAX_COLUMN 2000
#endif
/*
 * tt_static_buf() is used to store bitmask for used columns in a table during
 * SQL parsing stage. The following statement checks if static buffer is big
 * enough to store the bitmask.
 */
#if SQLITE_MAX_COLUMN > TT_STATIC_BUF_LEN * 8
#error "Bitmask for used table columns cannot fit into static buffer"
#endif

/*
 * The maximum length of a single SQL statement in bytes.
 *
 * It used to be the case that setting this value to zero would
 * turn the limit off.  That is no longer true.  It is not possible
 * to turn this limit off.
 */
#ifndef SQLITE_MAX_SQL_LENGTH
#define SQLITE_MAX_SQL_LENGTH 1000000000
#endif

/*
 * The maximum depth of an expression tree. This is limited to
 * some extent by SQLITE_MAX_SQL_LENGTH. But sometime you might
 * want to place more severe limits on the complexity of an
 * expression.
 *
 * A value of 0 used to mean that the limit was not enforced.
 * But that is no longer true.  The limit is now strictly enforced
 * at all times.
 */
#ifndef SQLITE_MAX_EXPR_DEPTH
#define SQLITE_MAX_EXPR_DEPTH 1000
#endif

/*
 * The maximum number of terms in a compound SELECT statement.
 * The code generator for compound SELECT statements does one
 * level of recursion for each term.  A stack overflow can result
 * if the number of terms is too large.  In practice, most SQL
 * never has more than 3 or 4 terms.  Use a value of 0 to disable
 * any limit on the number of terms in a compount SELECT.
 *
 * Tarantool: gh-2548: Fiber stack is 64KB by default, so maximum
 * number of entities should be less than 50 or stack guard will be
 * triggered.
 */
#ifndef SQLITE_MAX_COMPOUND_SELECT
#define SQLITE_MAX_COMPOUND_SELECT 50
#endif

/*
 * The maximum number of opcodes in a VDBE program.
 * Not currently enforced.
 */
#ifndef SQLITE_MAX_VDBE_OP
#define SQLITE_MAX_VDBE_OP 25000
#endif

/*
 * The maximum number of arguments to an SQL function.
 */
#ifndef SQLITE_MAX_FUNCTION_ARG
#define SQLITE_MAX_FUNCTION_ARG 127
#endif

/*
 * The suggested maximum number of in-memory pages to use for
 * the main database table and for temporary tables.
 *
 * IMPLEMENTATION-OF: R-30185-15359 The default suggested cache size is -2000,
 * which means the cache size is limited to 2048000 bytes of memory.
 * IMPLEMENTATION-OF: R-48205-43578 The default suggested cache size can be
 * altered using the SQLITE_DEFAULT_CACHE_SIZE compile-time options.
 */
#ifndef SQLITE_DEFAULT_CACHE_SIZE
#define SQLITE_DEFAULT_CACHE_SIZE  -2000
#endif

/*
 * The maximum number of attached databases.  This must be between 0
 * and 125.  The upper bound of 125 is because the attached databases are
 * counted using a signed 8-bit integer which has a maximum value of 127
 * and we have to allow 2 extra counts for the "main" and "temp" databases.
 */
#ifndef SQLITE_MAX_ATTACHED
#define SQLITE_MAX_ATTACHED 10
#endif

/*
 * Maximum length (in bytes) of the pattern in a LIKE or GLOB
 * operator.
 */
#ifndef SQLITE_MAX_LIKE_PATTERN_LENGTH
#define SQLITE_MAX_LIKE_PATTERN_LENGTH 50000
#endif

/*
 * Maximum depth of recursion for triggers.
 *
 * A value of 1 means that a trigger program will not be able to itself
 * fire any triggers. A value of 0 means that no trigger programs at all
 * may be executed.
 */
#ifndef SQLITE_MAX_TRIGGER_DEPTH
#define SQLITE_MAX_TRIGGER_DEPTH 1000
#endif

/*
 * Tarantool: gh-2550: Fiber stack is 64KB by default, so maximum
 * number of entities (in chain of compiling trigger programs) should be less than
 * 40 or stack guard will be triggered.
 */
#define SQL_MAX_COMPILING_TRIGGERS 30

#endif /* TARANTOOL_SQL_SQLITELIMIT_H_INCLUDED */
