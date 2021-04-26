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
 * Internal interface definitions for sql.
 *
 */
#ifndef sqlINT_H
#define sqlINT_H

#define IdChar(C)  ((sqlCtypeMap[(unsigned char)C]&0x46)!=0)

/* Special Comments:
 *
 * Some comments have special meaning to the tools that measure test
 * coverage:
 *
 *    NO_TEST                     - The branches on this line are not
 *                                  measured by branch coverage.  This is
 *                                  used on lines of code that actually
 *                                  implement parts of coverage testing.
 *
 *    OPTIMIZATION-IF-TRUE        - This branch is allowed to alway be false
 *                                  and the correct answer is still obtained,
 *                                  though perhaps more slowly.
 *
 *    OPTIMIZATION-IF-FALSE       - This branch is allowed to alway be true
 *                                  and the correct answer is still obtained,
 *                                  though perhaps more slowly.
 *
 *    PREVENTS-HARMLESS-OVERREAD  - This branch prevents a buffer overread
 *                                  that would be harmless and undetectable
 *                                  if it did occur.
 *
 * In all cases, the special comment must be enclosed in the usual
 * slash-asterisk...asterisk-slash comment marks, with no spaces between the
 * asterisks and the comment text.
 */

#include <stdbool.h>

#include "box/column_mask.h"
#include "parse_def.h"
#include "box/field_def.h"
#include "box/func.h"
#include "box/func_def.h"
#include "box/sql.h"
#include "box/txn.h"
#include "trivia/util.h"

/*
 * These #defines should enable >2GB file support on POSIX if the
 * underlying operating system supports it.  If the OS lacks
 * large file support, these should be no-ops.
 *
 * Ticket #2739:  The _LARGEFILE_SOURCE macro must appear before any
 * system #includes.  Hence, this block of code must be the very first
 * code in all source files.
 *
 * Large file support can be disabled using the -Dsql_DISABLE_LFS switch
 * on the compiler command line.  This is necessary if you are compiling
 * on a recent machine (ex: Red Hat 7.2) but you want your code to work
 * on an older machine (ex: Red Hat 6.0).  If you compile on Red Hat 7.2
 * without this option, LFS is enable.  But LFS does not exist in the kernel
 * in Red Hat 6.0, so the code won't work.  Hence, for maximum binary
 * portability you should omit LFS.
 *
 * The previous paragraph was written in 2005.  (This paragraph is written
 * on 2008-11-28.) These days, all Linux kernels support large files, so
 * you should probably leave LFS enabled.  But some embedded platforms might
 * lack LFS in which case the sql_DISABLE_LFS macro might still be useful.
 *
 * Similar is true for Mac OS X.  LFS is only supported on Mac OS X 9 and later.
 */
#ifndef SQL_DISABLE_LFS
#define _LARGE_FILE       1
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#define _LARGEFILE_SOURCE 1
#endif

/* What version of GCC is being used.  0 means GCC is not being used */
#ifdef __GNUC__
#define GCC_VERSION (__GNUC__*1000000+__GNUC_MINOR__*1000+__GNUC_PATCHLEVEL__)
#else
#define GCC_VERSION 0
#endif

/* Needed for various definitions... */
#if defined(__GNUC__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "sqlLimit.h"

/*
 * Include standard header files as necessary
 */
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/*
 * The following macros are used to cast pointers to integers and
 * integers to pointers.  The way you do this varies from one compiler
 * to the next, so we have developed the following set of #if statements
 * to generate appropriate macros for a wide range of compilers.
 *
 * The correct "ANSI" way to do this is to use the intptr_t type.
 * Unfortunately, that typedef is not available on all compilers, or
 * if it is available, it requires an #include of specific headers
 * that vary from one machine to the next.
 *
 * Ticket #3860:  The llvm-gcc-4.2 compiler from Apple chokes on
 * the ((void*)&((char*)0)[X]) construct.
 */
#if defined(__PTRDIFF_TYPE__)	/* This case should work for GCC */
#define SQL_INT_TO_PTR(X)  ((void*)(__PTRDIFF_TYPE__)(X))
#define SQL_PTR_TO_INT(X)  ((int)(__PTRDIFF_TYPE__)(X))
#elif !defined(__GNUC__)	/* Works for compilers other than LLVM */
#define SQL_INT_TO_PTR(X)  ((void*)&((char*)0)[X])
#define SQL_PTR_TO_INT(X)  ((int)(((char*)X)-(char*)0))
#elif defined(HAVE_STDINT_H)	/* Use this case if we have ANSI headers */
#define SQL_INT_TO_PTR(X)  ((void*)(intptr_t)(X))
#define SQL_PTR_TO_INT(X)  ((int)(intptr_t)(X))
#else				/* Generates a warning - but it always works */
#define SQL_INT_TO_PTR(X)  ((void*)(X))
#define SQL_PTR_TO_INT(X)  ((int)(X))
#endif

/*
 * A macro to hint to the compiler that a function should not be
 * inlined.
 */
#if defined(__GNUC__)
#define SQL_NOINLINE  __attribute__((noinline))
#else
#define SQL_NOINLINE
#endif

#if defined(SQL_SYSTEM_MALLOC) \
  + defined(SQL_ZERO_MALLOC) > 1
#error "Two or more of the following compile-time configuration options\
 are defined but at most one is allowed:\
 SQL_SYSTEM_MALLOC, SQL_ZERO_MALLOC"
#endif
#if defined(SQL_SYSTEM_MALLOC) \
  + defined(SQL_ZERO_MALLOC) == 0
#define SQL_SYSTEM_MALLOC 1
#endif

/*
 * If sql_MALLOC_SOFT_LIMIT is not zero, then try to keep the
 * sizes of memory allocations below this value where possible.
 */
#if !defined(SQL_MALLOC_SOFT_LIMIT)
#define SQL_MALLOC_SOFT_LIMIT 1024
#endif

/*
 * Enable sql_ENABLE_EXPLAIN_COMMENTS if sql_DEBUG is turned on.
 */
#if !defined(SQL_ENABLE_EXPLAIN_COMMENTS) && defined(SQL_DEBUG)
#define SQL_ENABLE_EXPLAIN_COMMENTS 1
#endif

/*
 * The testcase() macro is used to aid in coverage testing.  When
 * doing coverage testing, the condition inside the argument to
 * testcase() must be evaluated both true and false in order to
 * get full branch coverage.  The testcase() macro is inserted
 * to help ensure adequate test coverage in places where simple
 * condition/decision coverage is inadequate.  For example, testcase()
 * can be used to make sure boundary values are tested.  For
 * bitmask tests, testcase() can be used to make sure each bit
 * is significant and used at least once.  On switch statements
 * where multiple cases go to the same block of code, testcase()
 * can insure that all cases are evaluated.
 *
 */
#define testcase(X)

/*
 * The TESTONLY macro is used to enclose variable declarations or
 * other bits of code that are needed to support the arguments
 * within testcase() and assert() macros.
 */
#if !defined(NDEBUG)
#define TESTONLY(X)  X
#else
#define TESTONLY(X)
#endif

/*
 * Sometimes we need a small amount of code such as a variable initialization
 * to setup for a later assert() statement.  We do not want this code to
 * appear when assert() is disabled.  The following macro is therefore
 * used to contain that setup code.  The "VVA" acronym stands for
 * "Verification, Validation, and Accreditation".  In other words, the
 * code within VVA_ONLY() will only run during verification processes.
 */
#ifndef NDEBUG
#define VVA_ONLY(X)  X
#else
#define VVA_ONLY(X)
#endif

/*
 * The ALWAYS and NEVER macros surround boolean expressions which
 * are intended to always be true or false, respectively.  Such
 * expressions could be omitted from the code completely.  But they
 * are included in a few cases in order to enhance the resilience
 * of sql to unexpected behavior - to make the code "self-healing"
 * or "ductile" rather than being "brittle" and crashing at the first
 * hint of unplanned behavior.
 *
 * In other words, ALWAYS and NEVER are added for defensive code.
 */
#if !defined(NDEBUG)
#define ALWAYS(X)      ((X)?1:(assert(0),0))
#define NEVER(X)       ((X)?(assert(0),1):0)
#else
#define ALWAYS(X)      (X)
#define NEVER(X)       (X)
#endif


#include "hash.h"
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

typedef long long int sql_int64;
typedef unsigned long long int sql_uint64;
typedef sql_int64 sql_int64;
typedef sql_uint64 sql_uint64;
typedef struct sql_stmt sql_stmt;

typedef struct sql_context sql_context;
typedef struct sql sql;
typedef struct Mem sql_value;
typedef struct sql_file sql_file;
struct sql_file {
	const struct sql_io_methods *pMethods;	/* Methods for an open file */
};

typedef int (*sql_callback) (void *, int, char **, char **);

typedef struct sql_vfs sql_vfs;
struct sql_vfs {
	int iVersion;	/* Structure version number (currently 3) */
	int szOsFile;	/* Size of subclassed sql_file */
	int mxPathname;	/* Maximum file pathname length */
	sql_vfs *pNext;	/* Next registered VFS */
	const char *zName;	/* Name of this virtual file system */
	void *pAppData;	/* Pointer to application-specific data */
	int (*xOpen) (sql_vfs *, const char *zName, sql_file *,
		      int flags, int *pOutFlags);
	int (*xDelete) (sql_vfs *, const char *zName, int syncDir);
	int (*xRandomness) (sql_vfs *, int nByte, char *zOut);
	int (*xCurrentTime) (sql_vfs *, double *);
	/*
	** The methods above are in version 1 of the sql_vfs object
	** definition.  Those that follow are added in version 2 or later
	*/
	int (*xCurrentTimeInt64) (sql_vfs *, sql_int64 *);
	/*
	** The methods above are in versions 1 through 3 of the sql_vfs object.
	** New fields may be appended in future versions.  The iVersion
	** value will increment whenever this happens.
	*/
};

/**
 * Canonical string representation of SQL BOOLEAN values.
 * According to the standard it should be uppercase. See the 2011
 * standard, cast specification 6.13, general rules 11.e.
 */
#define SQL_TOKEN_TRUE "TRUE"
#define SQL_TOKEN_FALSE "FALSE"
#define SQL_TOKEN_BOOLEAN(v) ({(v) ? SQL_TOKEN_TRUE : SQL_TOKEN_FALSE;})

#define SQL_LIMIT_LENGTH                    0
#define SQL_LIMIT_SQL_LENGTH                1
#define SQL_LIMIT_COLUMN                    2
#define SQL_LIMIT_EXPR_DEPTH                3
#define SQL_LIMIT_COMPOUND_SELECT           4
#define SQL_LIMIT_VDBE_OP                   5
#define SQL_LIMIT_FUNCTION_ARG              6
#define SQL_LIMIT_ATTACHED                  7
#define SQL_LIMIT_LIKE_PATTERN_LENGTH       8
#define SQL_LIMIT_TRIGGER_DEPTH             9

enum sql_ret_code {
	/** sql_step() has another row ready. */
	SQL_ROW = 1,
	/** sql_step() has finished executing. */
	SQL_DONE = 2,
};

void *
sql_malloc(int);

void *
sql_malloc64(sql_uint64);

void *
sql_realloc64(void *, sql_uint64);

void
sql_free(void *);

int
sql_stricmp(const char *, const char *);

int
sql_strnicmp(const char *, const char *, int);

sql *
sql_context_db_handle(sql_context *);


void
sql_result_blob(sql_context *, const void *,
		    int, void (*)(void *));

void
sql_result_blob64(sql_context *, const void *,
		      sql_uint64, void (*)(void *));

void
sql_result_double(sql_context *, double);

void
sql_result_uint(sql_context *ctx, uint64_t u_val);

void
sql_result_int(sql_context *ctx, int64_t val);

void
sql_result_bool(struct sql_context *ctx, bool value);

void
sql_result_null(sql_context *);

void
sql_result_text(sql_context *, const char *,
		    int, void (*)(void *));

void
sql_result_text64(sql_context *, const char *,
		      sql_uint64, void (*)(void *));

void
sql_result_value(sql_context *,
		     sql_value *);

void
sql_result_zeroblob(sql_context *, int n);

int
sql_result_zeroblob64(sql_context *,
			  sql_uint64 n);

char *
sql_mprintf(const char *, ...);
char *
sql_vmprintf(const char *, va_list);
char *
sql_snprintf(int, char *, const char *, ...);
char *
sql_vsnprintf(int, char *, const char *, va_list);

/**
 * Wildcard characters used in REGEXP-like operators.
 */
#define MATCH_ONE_WILDCARD '_'
#define MATCH_ALL_WILDCARD '%'

typedef void (*sql_destructor_type) (void *);
#define SQL_STATIC      ((sql_destructor_type)0)
#define SQL_TRANSIENT   ((sql_destructor_type)-1)

/**
 * Compile the UTF-8 encoded SQL statement into
 * a statement handle (struct Vdbe).
 *
 * @param sql UTF-8 encoded SQL statement.
 * @param sql_len Length of @sql in bytes.
 * @param re_prepared VM being re-compiled. Can be NULL.
 * @param[out] stmt A pointer to the compiled statement.
 * @param[out] sql_tail End of parsed string.
 */
int
sql_stmt_compile(const char *sql, int bytes_count, struct Vdbe *re_prepared,
		 sql_stmt **stmt, const char **sql_tail);

int
sql_step(sql_stmt *);

int
sql_column_bytes16(sql_stmt *, int iCol);

char *
sql_stmt_result_to_msgpack(struct sql_stmt *stmt, uint32_t *tuple_size,
			   struct region *region);

/*
 * Terminate the current execution of an SQL statement and reset
 * it back to its starting state so that it can be reused.
 *
 * @param stmt VDBE program.
 * @retval 0 On success.
 * @retval sql_ret_code Error code on error.
 */
int
sql_stmt_reset(struct sql_stmt *stmt);

bool
sql_metadata_is_full();

int
sql_exec(sql *,	/* An open database */
	     const char *sql,	/* SQL to be evaluated */
	     int (*callback) (void *, int, char **, char **),	/* Callback function */
	     void *	/* 1st argument to callback */
	);

/**
 * Subtype of a main type. Allows to do some subtype specific
 * things: serialization, unpacking etc.
 */
enum sql_subtype {
	SQL_SUBTYPE_NO = 0,
	SQL_SUBTYPE_MSGPACK = 77,
};

void
sql_randomness(int N, void *P);

/**
 * Return the number of affected rows in the last SQL statement.
 */
void
sql_row_count(struct sql_context *context, MAYBE_UNUSED int unused1,
	      MAYBE_UNUSED sql_value **unused2);

void *
sql_aggregate_context(sql_context *,
			  int nBytes);

/**
 * Allocate or return the aggregate context containing struct MEM for a user
 * function. A new context is allocated on the first call. Subsequent calls
 * return the same context that was returned on prior calls.
 */
struct Mem *
sql_context_agg_mem(struct sql_context *context);

int
sql_column_count(sql_stmt * pStmt);

const char *
sql_column_name(sql_stmt *, int N);

const char *
sql_column_datatype(sql_stmt *, int N);

const char *
sql_column_coll(sql_stmt *stmt, int n);

int
sql_column_nullable(sql_stmt *stmt, int n);

bool
sql_column_is_autoincrement(sql_stmt *stmt, int n);

const char *
sql_column_span(sql_stmt *stmt, int n);

uint32_t
sql_stmt_schema_version(const struct sql_stmt *stmt);

int
sql_initialize(void);

#define SQL_TRACE_STMT       0x01
#define SQL_TRACE_PROFILE    0x02
#define SQL_TRACE_ROW        0x04
#define SQL_TRACE_CLOSE      0x08

#define SQL_OPEN_READONLY         0x00000001	/* Ok for sql_open_v2() */
#define SQL_OPEN_READWRITE        0x00000002	/* Ok for sql_open_v2() */
#define SQL_OPEN_CREATE           0x00000004	/* Ok for sql_open_v2() */
#define SQL_OPEN_DELETEONCLOSE    0x00000008	/* VFS only */
#define SQL_OPEN_EXCLUSIVE        0x00000010	/* VFS only */
#define SQL_OPEN_URI              0x00000040	/* Ok for sql_open_v2() */
#define SQL_OPEN_MAIN_DB          0x00000100	/* VFS only */

sql_vfs *
sql_vfs_find(const char *zVfsName);

typedef struct sql_io_methods sql_io_methods;
struct sql_io_methods {
	int iVersion;
	int (*xClose) (sql_file *);
	int (*xRead) (sql_file *, void *, int iAmt,
		      sql_int64 iOfst);
	int (*xWrite) (sql_file *, const void *, int iAmt,
		       sql_int64 iOfst);
	int (*xFileControl) (sql_file *, int op, void *pArg);
	/* Methods above are valid for version 2 */
	int (*xFetch) (sql_file *, sql_int64 iOfst, int iAmt,
		       void **pp);
	int (*xUnfetch) (sql_file *, sql_int64 iOfst, void *p);
	/* Methods above are valid for version 3 */
	/* Additional methods may be added in future releases */
};

#define SQL_FCNTL_LOCKSTATE               1
#define SQL_FCNTL_LAST_ERRNO              4
#define SQL_FCNTL_SIZE_HINT               5
#define SQL_FCNTL_CHUNK_SIZE              6
#define SQL_FCNTL_VFSNAME                11
#define SQL_FCNTL_TEMPFILENAME           15
#define SQL_FCNTL_MMAP_SIZE              16
#define SQL_FCNTL_HAS_MOVED              18

void
sql_os_init(void);

int
sql_limit(sql *, int id, int newVal);

extern char *
sql_temp_directory;

const char *
sql_uri_parameter(const char *zFilename,
		      const char *zParam);

const char *
sql_sql(sql_stmt * pStmt);

int
sql_vfs_register(sql_vfs *, int makeDflt);

#define SQL_STMTSTATUS_FULLSCAN_STEP     1
#define SQL_STMTSTATUS_SORT              2
#define SQL_STMTSTATUS_AUTOINDEX         3
#define SQL_STMTSTATUS_VM_STEP           4

/** Unbind all parameters of given prepared statement. */
void
sql_unbind(struct sql_stmt *stmt);

int
sql_bind_blob(sql_stmt *, int, const void *,
		  int n, void (*)(void *));

int
sql_bind_blob64(sql_stmt *, int, const void *,
		    sql_uint64, void (*)(void *));

int
sql_bind_double(sql_stmt *, int, double);

/**
 * Perform boolean parameter binding for the prepared sql
 * statement.
 * @param stmt Prepared statement.
 * @param i Index of the variable to be binded.
 * @param value Boolean value to use.
 * @retval 0 On Success, not 0 otherwise.
 */
int
sql_bind_boolean(struct sql_stmt *stmt, int i, bool value);

int
sql_bind_int(sql_stmt *, int, int);

int
sql_bind_int64(sql_stmt *, int, sql_int64);

int
sql_bind_uint64(struct sql_stmt *stmt, int i, uint64_t value);

int
sql_bind_null(sql_stmt *, int);

int
sql_bind_text64(sql_stmt *, int, const char *,
		    sql_uint64, void (*)(void *));

int
sql_bind_zeroblob(sql_stmt *, int, int n);

int
sql_bind_zeroblob64(sql_stmt *, int,
			sql_uint64);

/**
 * Return the number of wildcards that should be bound to.
 */
int
sql_bind_parameter_count(const struct sql_stmt *stmt);

/**
 * Return the name of a wildcard parameter. Return NULL if the index
 * is out of range or if the wildcard is unnamed. Parameter's index
 * is 0-based.
 */
const char *
sql_bind_parameter_name(const struct sql_stmt *stmt, int i);

/**
 * Perform pointer parameter binding for the prepared sql
 * statement.
 * @param stmt Prepared statement.
 * @param i Index of the variable to be binded.
 * @param ptr Pointer value to use.
 * @retval 0 On Success.
 * @retval Not 0 otherwise.
 */
int
sql_bind_ptr(struct sql_stmt *stmt, int i, void *ptr);

int
sql_init_db(sql **db);


/**
 * Get number of the named parameter in the prepared sql
 * statement.
 * @param pStmt Prepared statement.
 * @param zName Parameter name.
 * @param nName Parameter name length.
 *
 * @retval > 0 Number of the parameter.
 * @retval   0 Parameter is not found.
 */
int
sql_bind_parameter_lindex(sql_stmt * pStmt, const char *zName,
			      int nName);

/*
 * If compiling for a processor that lacks floating point support,
 * substitute integer for floating-point
 */
#ifndef SQL_BIG_DBL
#define SQL_BIG_DBL (1e99)
#endif

/*
 * OMIT_TEMPDB is set to 1 if sql_OMIT_TEMPDB is defined, or 0
 * afterward. Having this macro allows us to cause the C compiler
 * to omit code used by TEMP tables without messy #ifndef statements.
 */
#ifdef SQL_OMIT_TEMPDB
#define OMIT_TEMPDB 1
#else
#define OMIT_TEMPDB 0
#endif

/*
 * Determine whether triggers are recursive by default.  This can be
 * changed at run-time using a pragma.
 */
#ifndef SQL_DEFAULT_RECURSIVE_TRIGGERS
#define SQL_DEFAULT_RECURSIVE_TRIGGERS 0
#endif

/**
 * Default count of allowed compound selects.
 *
 * Tarantool: gh-2548, gh-3382: Fiber stack is 64KB by default,
 * so maximum number of entities should be less than 30 or stack
 * guard will be triggered (triggered with clang).
*/
#ifndef SQL_DEFAULT_COMPOUND_SELECT
#define SQL_DEFAULT_COMPOUND_SELECT 30
#endif

/*
 * GCC does not define the offsetof() macro so we'll have to do it
 * ourselves.
 */
#ifndef offsetof
#define offsetof(STRUCTURE,FIELD) ((int)((char*)&((STRUCTURE*)0)->FIELD))
#endif

/*
 * Macros to compute minimum and maximum of two numbers.
 */
#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif
#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif

/*
 * Integers of known sizes.  These typedefs might change for architectures
 * where the sizes very.  Preprocessor macros are available so that the
 * types can be conveniently redefined at compile-type.  Like this:
 *
 *         cc '-DUINTPTR_TYPE=long long int' ...
 */
#ifndef UINT32_TYPE
#ifdef HAVE_UINT32_T
#define UINT32_TYPE uint32_t
#else
#define UINT32_TYPE unsigned int
#endif
#endif
#ifndef UINT16_TYPE
#ifdef HAVE_UINT16_T
#define UINT16_TYPE uint16_t
#else
#define UINT16_TYPE unsigned short int
#endif
#endif
#ifndef INT16_TYPE
#ifdef HAVE_INT16_T
#define INT16_TYPE int16_t
#else
#define INT16_TYPE short int
#endif
#endif
#ifndef UINT8_TYPE
#ifdef HAVE_UINT8_T
#define UINT8_TYPE uint8_t
#else
#define UINT8_TYPE unsigned char
#endif
#endif
#ifndef INT8_TYPE
#ifdef HAVE_INT8_T
#define INT8_TYPE int8_t
#else
#define INT8_TYPE signed char
#endif
#endif
#ifndef LONGDOUBLE_TYPE
#define LONGDOUBLE_TYPE long double
#endif
typedef sql_int64 i64;	/* 8-byte signed integer */
typedef sql_uint64 u64;	/* 8-byte unsigned integer */
typedef UINT32_TYPE u32;	/* 4-byte unsigned integer */
typedef UINT16_TYPE u16;	/* 2-byte unsigned integer */
typedef INT16_TYPE i16;		/* 2-byte signed integer */
typedef UINT8_TYPE u8;		/* 1-byte unsigned integer */
typedef INT8_TYPE i8;		/* 1-byte signed integer */

/*
 * sql_MAX_U32 is a u64 constant that is the maximum u64 value
 * that can be stored in a u32 without loss of data.  The value
 * is 0x00000000ffffffff.  But because of quirks of some compilers, we
 * have to specify the value in the less intuitive manner shown:
 */
#define SQL_MAX_U32  ((((u64)1)<<32)-1)

/*
 * The datatype used to store estimates of the number of rows in a
 * table or index.  This is an unsigned integer type.  For 99.9% of
 * the world, a 32-bit integer is sufficient.  But a 64-bit integer
 * can be used at compile-time if desired.
 */
#ifdef SQL_64BIT_STATS
typedef u64 tRowcnt;		/* 64-bit only if requested at compile-time */
#else
typedef u32 tRowcnt;		/* 32-bit is the default */
#endif

/*
 * Estimated quantities used for query planning are stored as 16-bit
 * logarithms.  For quantity X, the value stored is 10*log2(X).  This
 * gives a possible range of values of approximately 1.0e986 to 1e-986.
 * But the allowed values are "grainy".  Not every value is representable.
 * For example, quantities 16 and 17 are both represented by a LogEst
 * of 40.  However, since LogEst quantities are suppose to be estimates,
 * not exact values, this imprecision is not a problem.
 *
 * "LogEst" is short for "Logarithmic Estimate".
 *
 * Examples:
 *      1 -> 0              20 -> 43          10000 -> 132
 *      2 -> 10             25 -> 46          25000 -> 146
 *      3 -> 16            100 -> 66        1000000 -> 199
 *      4 -> 20           1000 -> 99        1048576 -> 200
 *     10 -> 33           1024 -> 100    4294967296 -> 320
 *
 * The LogEst can be negative to indicate fractional values.
 * Examples:
 *
 *    0.5 -> -10           0.1 -> -33        0.0625 -> -40
 */
typedef INT16_TYPE LogEst;

/*
 * Set the sql_PTRSIZE macro to the number of bytes in a pointer
 */
#ifndef SQL_PTRSIZE
#if defined(__SIZEOF_POINTER__)
#define SQL_PTRSIZE __SIZEOF_POINTER__
#elif defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
       defined(_M_ARM)   || defined(__arm__)    || defined(__x86)
#define SQL_PTRSIZE 4
#else
#define SQL_PTRSIZE 8
#endif
#endif

/* The uptr type is an unsigned integer large enough to hold a pointer
 */
#if defined(HAVE_STDINT_H)
typedef uintptr_t uptr;
#elif SQL_PTRSIZE==4
typedef u32 uptr;
#else
typedef u64 uptr;
#endif

/*
 * The sql_WITHIN(P,S,E) macro checks to see if pointer P points to
 * something between S (inclusive) and E (exclusive).
 *
 * In other words, S is a buffer and E is a pointer to the first byte after
 * the end of buffer S.  This macro returns true if P points to something
 * contained within the buffer S.
 */
#define SQL_WITHIN(P,S,E) (((uptr)(P)>=(uptr)(S))&&((uptr)(P)<(uptr)(E)))

/*
 * Constants for the largest and smallest possible 64-bit signed integers.
 * These macros are designed to work correctly on both 32-bit and 64-bit
 * compilers.
 */
#define LARGEST_INT64  (0xffffffff|(((i64)0x7fffffff)<<32))
#define SMALLEST_INT64 (((i64)-1) - LARGEST_INT64)

/*
 * Round up a number to the next larger multiple of 8.  This is used
 * to force 8-byte alignment on 64-bit architectures.
 */
#define ROUND8(x)     (((x)+7)&~7)

/*
 * Round down to the nearest multiple of 8
 */
#define ROUNDDOWN8(x) ((x)&~7)

/*
 * Assert that the pointer X is aligned to an 8-byte boundary.  This
 * macro is used only within assert() to verify that the code gets
 * all alignment restrictions correct.
 *
 * Except, if sql_4_BYTE_ALIGNED_MALLOC is defined, then the
 * underlying malloc() implementation might return us 4-byte aligned
 * pointers.  In that case, only verify 4-byte alignment.
 */
#ifdef SQL_4_BYTE_ALIGNED_MALLOC
#define EIGHT_BYTE_ALIGNMENT(X)   ((((char*)(X) - (char*)0)&3)==0)
#else
#define EIGHT_BYTE_ALIGNMENT(X)   ((((char*)(X) - (char*)0)&7)==0)
#endif

/*
 * Default maximum size of memory used by memory-mapped I/O in the VFS
 */
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifndef SQL_MAX_MMAP_SIZE
#if defined(__linux__) \
  || (defined(__APPLE__) && defined(__MACH__))
#define SQL_MAX_MMAP_SIZE 0x7fff0000	/* 2147418112 */
#else
#define SQL_MAX_MMAP_SIZE 0
#endif
#define SQL_MAX_MMAP_SIZE_xc 1	/* exclude from ctime.c */
#endif

/*
 * The default MMAP_SIZE is zero on all platforms.  Or, even if a larger
 * default MMAP_SIZE is specified at compile-time, make sure that it does
 * not exceed the maximum mmap size.
 */
#ifndef SQL_DEFAULT_MMAP_SIZE
#define SQL_DEFAULT_MMAP_SIZE 0
#define SQL_DEFAULT_MMAP_SIZE_xc 1	/* Exclude from ctime.c */
#endif
#if SQL_DEFAULT_MMAP_SIZE>SQL_MAX_MMAP_SIZE
#undef SQL_DEFAULT_MMAP_SIZE
#define SQL_DEFAULT_MMAP_SIZE SQL_MAX_MMAP_SIZE
#endif

/*
 * A convenience macro that returns the number of elements in
 * an array.
 */
#define ArraySize(X)    ((int)(sizeof(X)/sizeof(X[0])))

/*
 * Determine if the argument is a power of two
 */
#define IsPowerOfTwo(X) (((X)&((X)-1))==0)

/*
 * The following value as a destructor means to use sqlDbFree().
 * The sqlDbFree() routine requires two parameters instead of the
 * one parameter that destructors normally want.  So we have to introduce
 * this magic value that the code knows to handle differently.  Any
 * pointer will work here as long as it is distinct from sql_STATIC
 * and sql_TRANSIENT.
 */
#define SQL_DYNAMIC   ((sql_destructor_type)sqlMallocSize)

/*
 * The usual case where Writable Static Data (WSD) is supported,
 * the sql_WSD and GLOBAL macros become no-ops and have zero
 * performance impact.
 */
#define SQL_WSD
#define GLOBAL(t,v) v
#define sqlGlobalConfig sqlConfig

/*
 * The following macros are used to suppress compiler warnings and to
 * make it clear to human readers when a function parameter is deliberately
 * left unused within the body of a function. This usually happens when
 * a function is called via a function pointer. For example the
 * implementation of an SQL aggregate step callback may not use the
 * parameter indicating the number of arguments passed to the aggregate,
 * if it knows that this is enforced elsewhere.
 *
 * When a function parameter is not used at all within the body of a function,
 * it is generally named "NotUsed" or "NotUsed2" to make things even clearer.
 * However, these macros may also be used to suppress warnings related to
 * parameters that may or may not be used depending on compilation options.
 * For example those parameters only used in assert() statements. In these
 * cases the parameters are named as per the usual conventions.
 */
#define UNUSED_PARAMETER(x) (void)(x)
#define UNUSED_PARAMETER2(x,y) UNUSED_PARAMETER(x),UNUSED_PARAMETER(y)

/*
 * Forward references to structures
 */
typedef struct AggInfo AggInfo;
typedef struct Bitvec Bitvec;
typedef struct Column Column;
typedef struct Expr Expr;
typedef struct ExprList ExprList;
typedef struct ExprSpan ExprSpan;
typedef struct IdList IdList;
typedef struct KeyClass KeyClass;
typedef struct Lookaside Lookaside;
typedef struct LookasideSlot LookasideSlot;
typedef struct NameContext NameContext;
typedef struct Parse Parse;
typedef struct PrintfArguments PrintfArguments;
typedef struct RowSet RowSet;
typedef struct Select Select;
typedef struct sqlThread sqlThread;
typedef struct SelectDest SelectDest;
typedef struct SrcList SrcList;
typedef struct StrAccum StrAccum;
typedef struct Token Token;
typedef struct TreeView TreeView;
typedef struct TriggerPrg TriggerPrg;
typedef struct TriggerStep TriggerStep;
typedef struct UnpackedRecord UnpackedRecord;
typedef struct Walker Walker;
typedef struct WhereInfo WhereInfo;
typedef struct With With;

/* A VList object records a mapping between parameters/variables/wildcards
 * in the SQL statement (such as $abc, @pqr, or :xyz) and the integer
 * variable number associated with that parameter.  See the format description
 * on the sqlVListAdd() routine for more information.  A VList is really
 * just an array of integers.
 */
typedef int VList;

/*
 * Defer sourcing vdbe.h and cursor.h until after the "u8" and
 * "BusyHandler" typedefs. vdbe.h also requires a few of the opaque
 * pointer types (i.e. FuncDef) defined above.
 */
#include "cursor.h"
#include "vdbe.h"
#include "os.h"

/*
 * The number of different kinds of things that can be limited
 * using the sql_limit() interface.
 */
#define SQL_N_LIMIT (SQL_LIMIT_TRIGGER_DEPTH+1)

/*
 * Lookaside malloc is a set of fixed-size buffers that can be used
 * to satisfy small transient memory allocation requests for objects
 * associated with a particular database connection.  The use of
 * lookaside malloc provides a significant performance enhancement
 * (approx 10%) by avoiding numerous malloc/free requests while parsing
 * SQL statements.
 *
 * The Lookaside structure holds configuration information about the
 * lookaside malloc subsystem.  Each available memory allocation in
 * the lookaside subsystem is stored on a linked list of LookasideSlot
 * objects.
 *
 * Lookaside allocations are only allowed for objects that are associated
 * with a particular database connection.  Hence, schema information cannot
 * be stored in lookaside because in shared cache mode the schema information
 * is shared by multiple database connections.  Therefore, while parsing
 * schema information, the Lookaside.bEnabled flag is cleared so that
 * lookaside allocations are not used to construct the schema objects.
 */
struct Lookaside {
	u32 bDisable;		/* Only operate the lookaside when zero */
	u16 sz;			/* Size of each buffer in bytes */
	u8 bMalloced;		/* True if pStart obtained from sql_malloc() */
	int nOut;		/* Number of buffers currently checked out */
	int mxOut;		/* Highwater mark for nOut */
	int anStat[3];		/* 0: hits.  1: size misses.  2: full misses */
	LookasideSlot *pFree;	/* List of available buffers */
	void *pStart;		/* First byte of available memory space */
	void *pEnd;		/* First byte past end of available space */
};
struct LookasideSlot {
	LookasideSlot *pNext;	/* Next buffer in the list of free buffers */
};

/*
 * Each database connection is an instance of the following structure.
 */
struct sql {
	sql_vfs *pVfs;	/* OS Interface */
	struct Vdbe *pVdbe;	/* List of active virtual machines */
	struct coll *pDfltColl;	/* The default collating sequence (BINARY) */
	i64 szMmap;		/* Default mmap_size setting */
	u16 dbOptFlags;		/* Flags to enable/disable optimizations */
	u8 enc;			/* Text encoding */
	u8 mallocFailed;	/* True if we have seen a malloc failure */
	u8 dfltLockMode;	/* Default locking-mode for attached dbs */
	u8 mTrace;		/* zero or more sql_TRACE flags */
	u32 magic;		/* Magic number for detect library misuse */
	/** Value returned by sql_row_count(). */
	int nChange;
	int aLimit[SQL_N_LIMIT];	/* Limits */
	int nMaxSorterMmap;	/* Maximum size of regions mapped by sorter */
	struct sqlInitInfo {	/* Information used during initialization */
		uint32_t space_id;
		uint32_t index_id;
		u8 busy;	/* TRUE if currently initializing */
		u8 orphanTrigger;	/* Last statement is orphaned TEMP trigger */
		u8 imposterTable;	/* Building an imposter table */
	} init;
	int nVdbeActive;	/* Number of VDBEs currently running */
	int nVdbeExec;		/* Number of nested calls to VdbeExec() */
	int (*xTrace) (u32, void *, void *, void *);	/* Trace function */
	void *pTraceArg;	/* Argument to the trace function */
	void (*xProfile) (void *, const char *, u64);	/* Profiling function */
	void *pProfileArg;	/* Argument to profile function */
	void *pCommitArg;	/* Argument to xCommitCallback() */
	int (*xCommitCallback) (void *);	/* Invoked at every commit. */
	void *pRollbackArg;	/* Argument to xRollbackCallback() */
	void (*xRollbackCallback) (void *);	/* Invoked at every commit. */
	void *pUpdateArg;
	void (*xUpdateCallback) (void *, int, const char *, const char *,
				 sql_int64);
	Lookaside lookaside;	/* Lookaside malloc configuration */
	Hash aFunc;		/* Hash table of connection functions */
};

/*
 * Possible values for the sql.flags.
 */
#define SQL_VdbeTrace      0x00000001	/* True to trace VDBE execution */
/* Debug print info about SQL query as it parsed */
#define PARSER_TRACE_FLAG  0x00000002
#define SQL_FullColNames   0x00000004	/* Show full column names on SELECT */
#define SQL_SqlTrace       0x00000200	/* Debug print SQL as it executes */
#define SQL_SelectTrace    0x00000800       /* Debug info about select statement */
#define SQL_WhereTrace     0x00008000       /* Debug info about optimizer's work */
#define SQL_VdbeListing    0x00000400	/* Debug listings of VDBE programs */
#define SQL_ReverseOrder   0x00020000	/* Reverse unordered SELECTs */
#define SQL_RecTriggers    0x00040000	/* Enable recursive triggers */
#define SQL_AutoIndex      0x00100000	/* Enable automatic indexes */
#define SQL_EnableTrigger  0x01000000	/* True to enable triggers */
#define SQL_DeferFKs       0x02000000	/* Defer all FK constraints */
#define SQL_VdbeEQP        0x08000000	/* Debug EXPLAIN QUERY PLAN */
#define SQL_FullMetadata   0x04000000	/* Display optional properties
					 * (nullability, autoincrement, alias)
					 * in metadata.
					 */

/* Bits of the sql.dbOptFlags field. */
#define SQL_QueryFlattener 0x0001	/* Query flattening */
#define SQL_ColumnCache    0x0002	/* Column cache */
#define SQL_GroupByOrder   0x0004	/* GROUPBY cover of ORDERBY */
#define SQL_FactorOutConst 0x0008	/* Constant factoring */
/*                not used    0x0010   // Was: sql_IdxRealAsInt */
#define SQL_DistinctOpt    0x0020	/* DISTINCT using indexes */
#define SQL_CoverIdxScan   0x0040	/* Covering index scans */
#define SQL_OrderByIdxJoin 0x0080	/* ORDER BY of joins via index */
#define SQL_SubqCoroutine  0x0100	/* Evaluate subqueries as coroutines */
#define SQL_Transitive     0x0200	/* Transitive constraints */
#define SQL_OmitNoopJoin   0x0400	/* Omit unused tables in joins */
#define SQL_AllOpts        0xffff	/* All optimizations */

/*
 * Macros for testing whether or not optimizations are enabled or disabled.
 */
#define OptimizationDisabled(db, mask)  (((db)->dbOptFlags&(mask))!=0)
#define OptimizationEnabled(db, mask)   (((db)->dbOptFlags&(mask))==0)

/*
 * Return true if it OK to factor constant expressions into the initialization
 * code. The argument is a Parse object for the code generator.
 */
#define ConstFactorOk(P) ((P)->okConstFactor)

/*
 * Possible values for the sql.magic field.
 * The numbers are obtained at random and have no special meaning, other
 * than being distinct from one another.
 */
#define SQL_MAGIC_OPEN     0xa029a697	/* Database is open */
#define SQL_MAGIC_BUSY     0xf03b7906	/* Database currently in use */

/**
 * SQL type definition. Now it is an alias to type, but in
 * future it will have some attributes like number of chars in
 * VARCHAR(<number of chars>).
 */
struct type_def {
	enum field_type type;
};

/*
 * Value constraints (enforced via assert()):
 *     NC_MinMaxAgg      == SF_MinMaxAgg
 *     SQL_FUNC_LENGTH    ==  OPFLAG_LENGTHARG
 *     SQL_FUNC_TYPEOF    ==  OPFLAG_TYPEOFARG
 */
#define SQL_FUNC_LIKE     0x0004	/* Candidate for the LIKE optimization */
#define SQL_FUNC_NEEDCOLL 0x0020	/* sqlGetFuncCollSeq() might be called.
					 * The flag is set when the collation
					 * of function arguments should be
					 * determined, using rules in
					 * collations_check_compatibility()
					 * function.
					 */
#define SQL_FUNC_LENGTH   0x0040	/* Built-in length() function */
#define SQL_FUNC_TYPEOF   0x0080	/* Built-in typeof() function */
/** Built-in count() function. */
#define SQL_FUNC_COUNT    0x0100
#define SQL_FUNC_COALESCE 0x0200	/* Built-in coalesce() or ifnull() */
#define SQL_FUNC_UNLIKELY 0x0400	/* Built-in unlikely() function */
/** Built-in min() or least() function. */
#define SQL_FUNC_MIN      0x1000
/** Built-in max() or greatest() function. */
#define SQL_FUNC_MAX      0x2000
/**
 * If function returns string, it may require collation to be
 * applied on its result. For instance, result of substr()
 * built-in function must have the same collation as its first
 * argument.
 */
#define SQL_FUNC_DERIVEDCOLL 0x4000

/*
 * Trim side mask components. TRIM_LEADING means to trim left side
 * only. TRIM_TRAILING is to trim right side only. TRIM_BOTH is to
 * trim both sides.
 */
enum trim_side_mask {
	TRIM_LEADING = 1,
	TRIM_TRAILING = 2,
	TRIM_BOTH = TRIM_LEADING | TRIM_TRAILING
};

/*
 * The following are used as the second parameter to sqlSavepoint(),
 * and as the P1 argument to the OP_Savepoint instruction.
 */
#define SAVEPOINT_BEGIN      0
#define SAVEPOINT_RELEASE    1
#define SAVEPOINT_ROLLBACK   2

#define sql_type_is_numeric(X)  ((X) == FIELD_TYPE_INTEGER || \
				 (X) == FIELD_TYPE_NUMBER || \
				 (X) == FIELD_TYPE_UNSIGNED || \
				 (X) == FIELD_TYPE_DOUBLE)

/*
 * Additional bit values that can be ORed with an type without
 * changing the type.
 *
 * The sql_NOTNULL flag is a combination of NULLEQ and JUMPIFNULL.
 * It causes an assert() to fire if either operand to a comparison
 * operator is NULL.  It is added to certain comparison operators to
 * prove that the operands are always NOT NULL.
 */
#define SQL_JUMPIFNULL   0x10	/* jumps if either operand is NULL */
#define SQL_STOREP2      0x20	/* Store result in reg[P2] rather than jump */
#define SQL_KEEPNULL     0x40	/* Used by vector == or <> */
#define SQL_NULLEQ       0x80	/* NULL=NULL */
#define SQL_NOTNULL      0x90	/* Assert that operands are never NULL */

/**
 * Return logarithm of tuple count in space.
 *
 * @param space Space to be examined.
 * @retval Logarithm of tuple count in space, or default values,
 *         if there is no corresponding space for given table.
 */
LogEst
sql_space_tuple_log_count(struct space *space);

/*
 * Each foreign key constraint is an instance of the following structure.
 *
 * A foreign key is associated with two tables.  The "from" table is
 * the table that contains the REFERENCES clause that creates the foreign
 * key.  The "to" table is the table that is named in the REFERENCES clause.
 * Consider this example:
 *
 *     CREATE TABLE ex1(
 *       a INTEGER PRIMARY KEY,
 *       b INTEGER CONSTRAINT fk1 REFERENCES ex2(x)
 *     );
 *
 * For foreign key "fk1", the from-table is "ex1" and the to-table is "ex2".
 * Equivalent names:
 *
 *     from-table == child-table
 *       to-table == parent-table
 *
 * Each REFERENCES clause generates an instance of the following structure
 * which is attached to the from-table.  The to-table need not exist when
 * the from-table is created.  The existence of the to-table is not checked.
 */

/*
 * This object holds a record which has been parsed out into individual
 * fields, for the purposes of doing a comparison.
 *
 * A record is an object that contains one or more fields of data.
 * Records are used to store the content of a table row and to store
 * the key of an index.  A blob encoding of a record is created by
 * the OP_MakeRecord opcode of the VDBE and is disassembled by the
 * OP_Column opcode.
 *
 * An instance of this object serves as a "key" for doing a search on
 * an index b+tree. The goal of the search is to find the entry that
 * is closed to the key described by this object.  This object might hold
 * just a prefix of the key.  The number of fields is given by
 * key_def->part_count.
 *
 * The r1 and r2 fields are the values to return if this key is less than
 * or greater than a key in the btree, respectively.  These are normally
 * -1 and +1 respectively, but might be inverted to +1 and -1 if the b-tree
 * is in DESC order.
 *
 * The key comparison functions actually return default_rc when they find
 * an equals comparison.  default_rc can be -1, 0, or +1.  If there are
 * multiple entries in the b-tree with the same key (when only looking
 * at the first key_def->part_count) then default_rc can be set to -1 to
 * cause the search to find the last match, or +1 to cause the search to
 * find the first match.
 *
 * The key comparison functions will set eqSeen to true if they ever
 * get and equal results when comparing this structure to a b-tree record.
 * When default_rc!=0, the search might end up on the record immediately
 * before the first match or immediately after the last match.  The
 * eqSeen field will indicate whether or not an exact match exists in the
 * b-tree.
 */
struct UnpackedRecord {
	/** Collation and sort-order information. */
	struct key_def *key_def;
	Mem *aMem;		/* Values */
	u16 nField;		/* Number of entries in apMem[] */
	i8 default_rc;		/* Comparison result if keys are equal */
	i8 r1;			/* Value to return if (lhs > rhs) */
	i8 r2;			/* Value to return if (rhs < lhs) */
	u8 eqSeen;		/* True if an equality comparison has been seen */
	u8 opcode;		/* Currently executing opcode that invoked
				 * movetoUnpacked, used by Tarantool storage layer.
				 */
};

/**
 * Fetch statistics concerning tuples to be selected:
 * logarithm of number of tuples which has the same key as for
 * the first key parts specified by second argument.
 * Simple example (without logarithms):
 * idx = {{1,2}, {1,2}, {2, 4}, {2, 3}, {3, 5}}
 * stat[1] = (2 + 2 + 1) / 3 = 2 - two keys which start from 1,
 * two - from 2, and one from 3.
 * stat[2] = (2 + 1 + 1 + 1) / 4 = 1 - only one coincidence of
 * keys: {1, 2}; the rest are different.
 * Notice, that stat[0] is an average number of tuples in a whole
 * index. By default it is DEFAULT_TUPLE_LOG_COUNT == 200.
 * If there is no appropriate Tarantool's index,
 * return one of default values.
 *
 * @param idx Index definition.
 * @param field Number of field to be examined.
 * @retval Estimate logarithm of tuples selected by given field.
 */
log_est_t
index_field_tuple_est(const struct index_def *idx, uint32_t field);

#ifdef DEFAULT_TUPLE_COUNT
#undef DEFAULT_TUPLE_COUNT
#endif
#define DEFAULT_TUPLE_COUNT 1048576
/** [10*log_{2}(1048576)] == 200 */
#define DEFAULT_TUPLE_LOG_COUNT 200

/*
 * An instance of this structure contains information needed to generate
 * code for a SELECT that contains aggregate functions.
 *
 * If Expr.op==TK_AGG_COLUMN or TK_AGG_FUNCTION then Expr.pAggInfo is a
 * pointer to this structure.  The Expr.iColumn field is the index in
 * AggInfo.aCol[] or AggInfo.aFunc[] of information needed to generate
 * code for that node.
 *
 * AggInfo.pGroupBy and AggInfo.aFunc.pExpr point to fields within the
 * original Select structure that describes the SELECT statement.  These
 * fields do not need to be freed when deallocating the AggInfo structure.
 */
struct AggInfo {
	u8 directMode;		/* Direct rendering mode means take data directly
				 * from source tables rather than from accumulators
				 */
	u8 useSortingIdx;	/* In direct mode, reference the sorting index rather
				 * than the source table
				 */
	int sortingIdx;		/* Cursor number of the sorting index */
	int sortingIdxPTab;	/* Cursor number of pseudo-table */
	int nSortingColumn;	/* Number of columns in the sorting index */
	int mnReg, mxReg;	/* Range of registers allocated for aCol and aFunc */
	ExprList *pGroupBy;	/* The group by clause */
	struct AggInfo_col {	/* For each column used in source tables */
		/** Pointer to space definition. */
		struct space_def *space_def;
		int iTable;	/* Cursor number of the source table */
		int iColumn;	/* Column number within the source table */
		int iSorterColumn;	/* Column number in the sorting index */
		int iMem;	/* Memory location that acts as accumulator */
		Expr *pExpr;	/* The original expression */
	} *aCol;
	int nColumn;		/* Number of used entries in aCol[] */
	int nAccumulator;	/* Number of columns that show through to the output.
				 * Additional columns are used only as parameters to
				 * aggregate functions
				 */
	struct AggInfo_func {	/* For each aggregate function */
		Expr *pExpr;	/* Expression encoding the function */
		/** The aggregate function implementation. */
		struct func *func;
		int iMem;	/* Memory location that acts as accumulator */
		int iDistinct;	/* Ephemeral table used to enforce DISTINCT */
		/**
		 * Register, holding ephemeral's space pointer.
		 */
		int reg_eph;
	} *aFunc;
	int nFunc;		/* Number of entries in aFunc[] */
};

typedef int ynVar;

/*
 * Each node of an expression in the parse tree is an instance
 * of this structure.
 *
 * Expr.op is the opcode. The integer parser token codes are reused
 * as opcodes here. For example, the parser defines TK_GE to be an integer
 * code representing the ">=" operator. This same integer code is reused
 * to represent the greater-than-or-equal-to operator in the expression
 * tree.
 *
 * If the expression is an SQL literal (TK_INTEGER, TK_FLOAT, TK_BLOB,
 * or TK_STRING), then Expr.token contains the text of the SQL literal. If
 * the expression is a variable (TK_VARIABLE), then Expr.token contains the
 * variable name. Finally, if the expression is an SQL function (TK_FUNCTION),
 * then Expr.token contains the name of the function.
 *
 * Expr.pRight and Expr.pLeft are the left and right subexpressions of a
 * binary operator. Either or both may be NULL.
 *
 * Expr.x.pList is a list of arguments if the expression is an SQL function,
 * a CASE expression or an IN expression of the form "<lhs> IN (<y>, <z>...)".
 * Expr.x.pSelect is used if the expression is a sub-select or an expression of
 * the form "<lhs> IN (SELECT ...)". If the EP_xIsSelect bit is set in the
 * Expr.flags mask, then Expr.x.pSelect is valid. Otherwise, Expr.x.pList is
 * valid.
 *
 * An expression of the form ID or ID.ID refers to a column in a table.
 * For such expressions, Expr.op is set to TK_COLUMN_REF and Expr.iTable is
 * the integer cursor number of a VDBE cursor pointing to that table and
 * Expr.iColumn is the column number for the specific column.  If the
 * expression is used as a result in an aggregate SELECT, then the
 * value is also stored in the Expr.iAgg column in the aggregate so that
 * it can be accessed after all aggregates are computed.
 *
 * If the expression is an unbound variable marker (a question mark
 * character '?' in the original SQL) then the Expr.iTable holds the index
 * number for that variable.
 *
 * If the expression is a subquery then Expr.iColumn holds an integer
 * register number containing the result of the subquery.  If the
 * subquery gives a constant result, then iTable is -1.  If the subquery
 * gives a different answer at different times during statement processing
 * then iTable is the address of a subroutine that computes the subquery.
 *
 * If the Expr is of type OP_Column, and the table it is selecting from
 * is a disk table or the "old.*" pseudo-table, then space_def points to the
 * corresponding table definition.
 *
 * ALLOCATION NOTES:
 *
 * Expr objects can use a lot of memory space in database schema.  To
 * help reduce memory requirements, sometimes an Expr object will be
 * truncated.  And to reduce the number of memory allocations, sometimes
 * two or more Expr objects will be stored in a single memory allocation,
 * together with Expr.zToken strings.
 *
 * If the EP_Reduced and EP_TokenOnly flags are set when
 * an Expr object is truncated.  When EP_Reduced is set, then all
 * the child Expr objects in the Expr.pLeft and Expr.pRight subtrees
 * are contained within the same memory allocation.  Note, however, that
 * the subtrees in Expr.x.pList or Expr.x.pSelect are always separately
 * allocated, regardless of whether or not EP_Reduced is set.
 */
struct Expr {
	u8 op;			/* Operation performed by this node */
	union {
		/** The type of the column. */
		enum field_type type;
		/** Conflict action for RAISE() function. */
		enum on_conflict_action on_conflict_action;
	};
	u32 flags;		/* Various flags.  EP_* See below */
	union {
		char *zToken;	/* Token value. Zero terminated and dequoted */
		int iValue;	/* Non-negative integer value if EP_IntValue */
	} u;

	/* If the EP_TokenOnly flag is set in the Expr.flags mask, then no
	 * space is allocated for the fields below this point. An attempt to
	 * access them will result in a segfault or malfunction.
	 ********************************************************************/

	Expr *pLeft;		/* Left subnode */
	Expr *pRight;		/* Right subnode */
	union {
		ExprList *pList;	/* op = IN, EXISTS, SELECT, CASE, FUNCTION, BETWEEN */
		Select *pSelect;	/* EP_xIsSelect and op = IN, EXISTS, SELECT */
	} x;

	/* If the EP_Reduced flag is set in the Expr.flags mask, then no
	 * space is allocated for the fields below this point. An attempt to
	 * access them will result in a segfault or malfunction.
	 ********************************************************************/

#if SQL_MAX_EXPR_DEPTH>0
	int nHeight;		/* Height of the tree headed by this node */
#endif
	int iTable;		/* TK_COLUMN_REF: cursor number of table holding column
				 * TK_REGISTER: register number
				 * TK_TRIGGER: 1 -> new, 0 -> old
				 * EP_Unlikely:  134217728 times likelihood
				 * TK_SELECT: 1st register of result vector
				 */
	ynVar iColumn;		/* TK_COLUMN_REF: column index.
				 * TK_VARIABLE: variable number (always >= 1).
				 * TK_SELECT_COLUMN: column of the result vector
				 */
	i16 iAgg;		/* Which entry in pAggInfo->aCol[] or ->aFunc[] */
	i16 iRightJoinTable;	/* If EP_FromJoin, the right table of the join */
	u8 op2;			/* TK_REGISTER: original value of Expr.op
				 * TK_COLUMN_REF: the value of p5 for OP_Column
				 * TK_AGG_FUNCTION: nesting depth
				 */
	AggInfo *pAggInfo;	/* Used by TK_AGG_COLUMN and TK_AGG_FUNCTION */
	/** Pointer for table relative definition. */
	struct space_def *space_def;
};

/*
 * The following are the meanings of bits in the Expr.flags field.
 */
#define EP_FromJoin  0x000001	/* Originates in ON/USING clause of outer join */
#define EP_Agg       0x000002	/* Contains one or more aggregate functions */
#define EP_Resolved  0x000004	/* IDs have been resolved to COLUMNs */
#define EP_Error     0x000008	/* Expression contains one or more errors */
#define EP_Distinct  0x000010	/* Aggregate function with DISTINCT keyword */
#define EP_VarSelect 0x000020	/* pSelect is correlated, not constant */
#define EP_DblQuoted 0x000040	/* token.z was originally in "..." */
#define EP_InfixFunc 0x000080	/* True for an infix function: LIKE, etc */
#define EP_Collate   0x000100	/* Tree contains a TK_COLLATE operator */
#define EP_IntValue  0x000400	/* Integer value contained in u.iValue */
#define EP_xIsSelect 0x000800	/* x.pSelect is valid (otherwise x.pList is) */
#define EP_Skip      0x001000	/* COLLATE, AS, or UNLIKELY */
#define EP_Reduced   0x002000	/* Expr struct EXPR_REDUCEDSIZE bytes only */
#define EP_TokenOnly 0x004000	/* Expr struct EXPR_TOKENONLYSIZE bytes only */
#define EP_Static    0x008000	/* Held in memory not obtained from malloc() */
#define EP_MemToken  0x010000	/* Need to sqlDbFree() Expr.zToken */
#define EP_NoReduce  0x020000	/* Cannot EXPRDUP_REDUCE this Expr */
#define EP_Unlikely  0x040000	/* unlikely() or likelihood() function */
#define EP_ConstFunc 0x080000	/* A sql_FUNC_CONSTANT or _SLOCHNG function */
#define EP_CanBeNull 0x100000	/* Can be null despite NOT NULL constraint */
#define EP_Subquery  0x200000	/* Tree contains a TK_SELECT operator */
#define EP_Alias     0x400000	/* Is an alias for a result set column */
#define EP_Leaf      0x800000	/* Expr.pLeft, .pRight, .u.pSelect all NULL */
/** Expression is system-defined. */
#define EP_System    0x1000000

/*
 * Combinations of two or more EP_* flags
 */
#define EP_Propagate (EP_Collate|EP_Subquery)	/* Propagate these bits up tree */

/*
 * These macros can be used to test, set, or clear bits in the
 * Expr.flags field.
 */
#define ExprHasProperty(E,P)     (((E)->flags&(P))!=0)
#define ExprHasAllProperty(E,P)  (((E)->flags&(P))==(P))
#define ExprSetProperty(E,P)     (E)->flags|=(P)
#define ExprClearProperty(E,P)   (E)->flags&=~(P)

/* The ExprSetVVAProperty() macro is used for Verification, Validation,
 * and Accreditation only.  It works like ExprSetProperty() during VVA
 * processes but is a no-op for delivery.
 */
#ifdef SQL_DEBUG
#define ExprSetVVAProperty(E,P)  (E)->flags|=(P)
#else
#define ExprSetVVAProperty(E,P)
#endif

/*
 * Macros to determine the number of bytes required by a normal Expr
 * struct, an Expr struct with the EP_Reduced flag set in Expr.flags
 * and an Expr struct with the EP_TokenOnly flag set.
 */
#define EXPR_FULLSIZE           sizeof(Expr)	/* Full size */
#define EXPR_REDUCEDSIZE        offsetof(Expr,iTable)	/* Common features */
#define EXPR_TOKENONLYSIZE      offsetof(Expr,pLeft)	/* Fewer features */

/*
 * Flags passed to the sqlExprDup() function. See the header comment
 * above sqlExprDup() for details.
 */
#define EXPRDUP_REDUCE         0x0001	/* Used reduced-size Expr nodes */

/*
 * A list of expressions.  Each expression may optionally have a
 * name.  An expr/name combination can be used in several ways, such
 * as the list of "expr AS ID" fields following a "SELECT" or in the
 * list of "ID = expr" items in an UPDATE.  A list of expressions can
 * also be used as the argument to a function, in which case the a.zName
 * field is not used.
 *
 * By default the Expr.zSpan field holds a human-readable description of
 * the expression that is used in the generation of error messages and
 * column labels.  In this case, Expr.zSpan is typically the text of a
 * column expression as it exists in a SELECT statement.  However, if
 * the bSpanIsTab flag is set, then zSpan is overloaded to mean the name
 * of the result column in the form: DATABASE.TABLE.COLUMN.  This later
 * form is used for name resolution with nested FROM clauses.
 */
struct ExprList {
	int nExpr;		/* Number of expressions on the list */
	struct ExprList_item {	/* For each expression in the list */
		Expr *pExpr;	/* The list of expressions */
		char *zName;	/* Token associated with this expression */
		char *zSpan;	/* Original text of the expression */
		enum sort_order sort_order;
		unsigned done:1;	/* A flag to indicate when processing is finished */
		unsigned bSpanIsTab:1;	/* zSpan holds DB.TABLE.COLUMN */
		unsigned reusable:1;	/* Constant expression is reusable */
		union {
			struct {
				u16 iOrderByCol;	/* For ORDER BY, column number in result set */
				u16 iAlias;	/* Index into Parse.aAlias[] for zName */
			} x;
			int iConstExprReg;	/* Register in which Expr value is cached */
		} u;
	} *a;			/* Alloc a power of two greater or equal to nExpr */
};

/*
 * An instance of this structure is used by the parser to record both
 * the parse tree for an expression and the span of input text for an
 * expression.
 */
struct ExprSpan {
	Expr *pExpr;		/* The expression parse tree */
	const char *zStart;	/* First character of input text */
	const char *zEnd;	/* One character past the end of input text */
};

/*
 * An instance of this structure can hold a simple list of identifiers,
 * such as the list "a,b,c" in the following statements:
 *
 *      INSERT INTO t(a,b,c) VALUES ...;
 *      CREATE INDEX idx ON t(a,b,c);
 *      CREATE TRIGGER trig BEFORE UPDATE ON t(a,b,c) ...;
 *
 * The IdList.a.idx field is used when the IdList represents the list of
 * column names after a table name in an INSERT statement.  In the statement
 *
 *     INSERT INTO t(a,b,c) ...
 *
 * If "a" is the k-th column of table "t", then IdList.a[0].idx==k.
 */
struct IdList {
	struct IdList_item {
		char *zName;	/* Name of the identifier */
		int idx;	/* Index in some Table.aCol[] of a column named zName */
	} *a;
	int nId;		/* Number of identifiers on the list */
};

/*
 * The bitmask datatype defined below is used for various optimizations.
 *
 * Changing this from a 64-bit to a 32-bit type limits the number of
 * tables in a join to 32 instead of 64.  But it also reduces the size
 * of the library by 738 bytes on ix86.
 */
#ifdef SQL_BITMASK_TYPE
typedef SQL_BITMASK_TYPE Bitmask;
#else
typedef u64 Bitmask;
#endif

/*
 * The number of bits in a Bitmask.  "BMS" means "BitMask Size".
 */
#define BMS  ((int)(sizeof(Bitmask)*8))

/*
 * A bit in a Bitmask
 */
#define MASKBIT(n)   (((Bitmask)1)<<(n))
#define MASKBIT32(n) (((unsigned int)1)<<(n))
#define ALLBITS      ((Bitmask)-1)

/*
 * The following structure describes the FROM clause of a SELECT statement.
 * Each table or subquery in the FROM clause is a separate element of
 * the SrcList.a[] array.
 *
 * With the addition of multiple database support, the following structure
 * can also be used to describe a particular table such as the table that
 * is modified by an INSERT, DELETE, or UPDATE statement.  In standard SQL,
 * such a table must be a simple name: ID.  But in sql, the table can
 * now be identified by a database name, a dot, then the table name: ID.ID.
 *
 * The jointype starts out showing the join type between the current table
 * and the next table on the list.  The parser builds the list this way.
 * But sqlSrcListShiftJoinType() later shifts the jointypes so that each
 * jointype expresses the join between the table and the previous table.
 *
 * In the colUsed field, the high-order bit (bit 63) is set if the table
 * contains more than 63 columns and the 64-th or later column is used.
 */
struct SrcList {
	int nSrc;		/* Number of tables or subqueries in the FROM clause */
	u32 nAlloc;		/* Number of entries allocated in a[] below */
	struct SrcList_item {
		char *zName;	/* Name of the table */
		char *zAlias;	/* The "B" part of a "A AS B" phrase.  zName is the "A" */
		/** A space corresponding to zName */
		struct space *space;
		Select *pSelect;	/* A SELECT statement used in place of a table name */
		int addrFillSub;	/* Address of subroutine to manifest a subquery */
		int regReturn;	/* Register holding return address of addrFillSub */
		int regResult;	/* Registers holding results of a co-routine */
		struct {
			u8 jointype;	/* Type of join between this table and the previous */
			unsigned notIndexed:1;	/* True if there is a NOT INDEXED clause */
			unsigned isIndexedBy:1;	/* True if there is an INDEXED BY clause */
			unsigned isTabFunc:1;	/* True if table-valued-function syntax */
			unsigned isCorrelated:1;	/* True if sub-query is correlated */
			unsigned viaCoroutine:1;	/* Implemented as a co-routine */
			unsigned isRecursive:1;	/* True for recursive reference in WITH */
		} fg;
		u8 iSelectId;	/* If pSelect!=0, the id of the sub-select in EQP */
		int iCursor;	/* The VDBE cursor number used to access this table */
		Expr *pOn;	/* The ON clause of a join */
		IdList *pUsing;	/* The USING clause of a join */
		Bitmask colUsed;	/* Bit N (1<<N) set if column N of space is used */
		union {
			char *zIndexedBy;	/* Identifier from "INDEXED BY <zIndex>" clause */
			ExprList *pFuncArg;	/* Arguments to table-valued-function */
		} u1;
		struct index_def *pIBIndex;
	} a[1];			/* One entry for each identifier on the list */
};

/*
 * Permitted values of the SrcList.a.jointype field
 */
#define JT_INNER     0x0001	/* Any kind of inner or cross join */
#define JT_CROSS     0x0002	/* Explicit use of the CROSS keyword */
#define JT_NATURAL   0x0004	/* True for a "natural" join */
#define JT_LEFT      0x0008	/* Left outer join */
#define JT_RIGHT     0x0010	/* Right outer join */
#define JT_OUTER     0x0020	/* The "OUTER" keyword is present */
#define JT_ERROR     0x0040	/* unknown or unsupported join type */

/*
 * Flags appropriate for the wctrlFlags parameter of sqlWhereBegin()
 * and the WhereInfo.wctrlFlags member.
 *
 * Value constraints (enforced via assert()):
 *     WHERE_USE_LIMIT  == SF_FixedLimit
 */
#define WHERE_ORDERBY_NORMAL   0x0000	/* No-op */
#define WHERE_ORDERBY_MIN      0x0001	/* ORDER BY processing for min() func */
#define WHERE_ORDERBY_MAX      0x0002	/* ORDER BY processing for max() func */
#define WHERE_ONEPASS_DESIRED  0x0004	/* Want to do one-pass UPDATE/DELETE */
#define WHERE_ONEPASS_MULTIROW 0x0008	/* ONEPASS is ok with multiple rows */
#define WHERE_DUPLICATES_OK    0x0010	/* Ok to return a row more than once */
#define WHERE_OR_SUBCLAUSE     0x0020	/* Processing a sub-WHERE as part of
					 * the OR optimization
					 */
#define WHERE_GROUPBY          0x0040	/* pOrderBy is really a GROUP BY */
#define WHERE_DISTINCTBY       0x0080	/* pOrderby is really a DISTINCT clause */
#define WHERE_WANT_DISTINCT    0x0100	/* All output needs to be distinct */
#define WHERE_SORTBYGROUP      0x0200	/* Support sqlWhereIsSorted() */
#define WHERE_SEEK_TABLE       0x0400	/* Do not defer seeks on main table */
#define WHERE_ORDERBY_LIMIT    0x0800	/* ORDERBY+LIMIT on the inner loop */
			/*     0x1000    not currently used */
			/*     0x2000    not currently used */
#define WHERE_USE_LIMIT        0x4000	/* Use the LIMIT in cost estimates */
			/*     0x8000    not currently used */

/* Allowed return values from sqlWhereIsDistinct()
 */
#define WHERE_DISTINCT_NOOP      0	/* DISTINCT keyword not used */
#define WHERE_DISTINCT_UNIQUE    1	/* No duplicates */
#define WHERE_DISTINCT_ORDERED   2	/* All duplicates are adjacent */
#define WHERE_DISTINCT_UNORDERED 3	/* Duplicates are scattered */

/*
 * A NameContext defines a context in which to resolve table and column
 * names.  The context consists of a list of tables (the pSrcList) field and
 * a list of named expression (pEList).  The named expression list may
 * be NULL.  The pSrc corresponds to the FROM clause of a SELECT or
 * to the table being operated on by INSERT, UPDATE, or DELETE.  The
 * pEList corresponds to the result set of a SELECT and is NULL for
 * other statements.
 *
 * NameContexts can be nested.  When resolving names, the inner-most
 * context is searched first.  If no match is found, the next outer
 * context is checked.  If there is still no match, the next context
 * is checked.  This process continues until either a match is found
 * or all contexts are check.  When a match is found, the nRef member of
 * the context containing the match is incremented.
 *
 * Each subquery gets a new NameContext.  The pNext field points to the
 * NameContext in the parent query.  Thus the process of scanning the
 * NameContext list corresponds to searching through successively outer
 * subqueries looking for a match.
 */
struct NameContext {
	Parse *pParse;		/* The parser */
	SrcList *pSrcList;	/* One or more tables used to resolve names */
	ExprList *pEList;	/* Optional list of result-set columns */
	AggInfo *pAggInfo;	/* Information about aggregates at this level */
	NameContext *pNext;	/* Next outer name context.  NULL for outermost */
	int nRef;		/* Number of names resolved by this context */
	int nErr;		/* Number of errors encountered while resolving names */
	u16 ncFlags;		/* Zero or more NC_* flags defined below */
};

/*
 * Allowed values for the NameContext, ncFlags field.
 *
 * Value constraints (all checked via assert()):
 *    NC_HasAgg    == SF_HasAgg
 *    NC_MinMaxAgg == SF_MinMaxAgg
 *
 */
#define NC_AllowAgg  0x0001	/* Aggregate functions are allowed here */
#define NC_IsCheck   0x0004	/* True if resolving names in a CHECK constraint */
#define NC_InAggFunc 0x0008	/* True if analyzing arguments to an agg func */
#define NC_HasAgg    0x0010	/* One or more aggregate functions seen */
#define NC_IdxExpr   0x0020	/* True if resolving columns of CREATE INDEX */
#define NC_VarSelect 0x0040	/* A correlated subquery has been seen */
#define NC_MinMaxAgg 0x1000	/* min/max aggregates seen.  See note above */
/** One or more identifiers are out of aggregate function. */
#define NC_HasUnaggregatedId     0x2000
/*
 * An instance of the following structure contains all information
 * needed to generate code for a single SELECT statement.
 *
 * nLimit is set to -1 if there is no LIMIT clause.  nOffset is set to 0.
 * If there is a LIMIT clause, the parser sets nLimit to the value of the
 * limit and nOffset to the value of the offset (or 0 if there is not
 * offset).  But later on, nLimit and nOffset become the memory locations
 * in the VDBE that record the limit and offset counters.
 *
 * addrOpenEphm[] entries contain the address of OP_OpenEphemeral opcodes.
 * These addresses must be stored so that we can go back and fill in
 * the P4_KEYINFO and P2 parameters later.  Neither the key_info nor
 * the number of columns in P2 can be computed at the same time
 * as the OP_OpenEphm instruction is coded because not
 * enough information about the compound query is known at that point.
 * The key_info for addrOpenTran[0] and [1] contains collating sequences
 * for the result set. The key_info for addrOpenEphm[2] contains collating
 * sequences for the ORDER BY clause.
 */
struct Select {
	ExprList *pEList;	/* The fields of the result */
	u8 op;			/* One of: TK_UNION TK_ALL TK_INTERSECT TK_EXCEPT */
	LogEst nSelectRow;	/* Estimated number of result rows */
	u32 selFlags;		/* Various SF_* values */
	int iLimit, iOffset;	/* Memory registers holding LIMIT & OFFSET counters */
	char zSelName[12];	/* Symbolic name of this SELECT use for debugging */
	int addrOpenEphm[2];	/* OP_OpenEphem opcodes related to this select */
	SrcList *pSrc;		/* The FROM clause */
	Expr *pWhere;		/* The WHERE clause */
	ExprList *pGroupBy;	/* The GROUP BY clause */
	Expr *pHaving;		/* The HAVING clause */
	ExprList *pOrderBy;	/* The ORDER BY clause */
	Select *pPrior;		/* Prior select in a compound select statement */
	Select *pNext;		/* Next select to the left in a compound */
	Expr *pLimit;		/* LIMIT expression. NULL means not used. */
	Expr *pOffset;		/* OFFSET expression. NULL means not used. */
	With *pWith;		/* WITH clause attached to this select. Or NULL. */
};

/*
 * Allowed values for Select.selFlags.  The "SF" prefix stands for
 * "Select Flag".
 *
 * Value constraints (all checked via assert())
 *     SF_HasAgg     == NC_HasAgg
 *     SF_MinMaxAgg  == NC_MinMaxAgg
 *     SF_FixedLimit == WHERE_USE_LIMIT
 */
#define SF_Distinct       0x00001	/* Output should be DISTINCT */
#define SF_All            0x00002	/* Includes the ALL keyword */
#define SF_Resolved       0x00004	/* Identifiers have been resolved */
#define SF_Aggregate      0x00008	/* Contains agg functions or a GROUP BY */
#define SF_HasAgg         0x00010	/* Contains aggregate functions */
#define SF_UsesEphemeral  0x00020	/* Uses the OpenEphemeral opcode */
#define SF_Expanded       0x00040	/* sqlSelectExpand() called on this */
#define SF_HasTypeInfo    0x00080	/* FROM subqueries have Table metadata */
#define SF_Compound       0x00100	/* Part of a compound query */
#define SF_Values         0x00200	/* Synthesized from VALUES clause */
#define SF_MultiValue     0x00400	/* Single VALUES term with multiple rows */
#define SF_NestedFrom     0x00800	/* Part of a parenthesized FROM clause */
#define SF_MinMaxAgg      0x01000	/* Aggregate containing min() or max() */
#define SF_Recursive      0x02000	/* The recursive part of a recursive CTE */
#define SF_FixedLimit     0x04000	/* nSelectRow set by a constant LIMIT */
#define SF_MaybeConvert   0x08000	/* Need convertCompoundSelectToSubquery() */
#define SF_Converted      0x10000	/* By convertCompoundSelectToSubquery() */
/** Abort subquery if its output contains more than one row. */
#define SF_SingleRow      0x20000

/*
 * The results of a SELECT can be distributed in several ways, as defined
 * by one of the following macros.  The "SRT" prefix means "SELECT Result
 * Type".
 *
 *     SRT_Union       Store results as a key in a temporary index
 *                     identified by pDest->iSDParm.
 *
 *     SRT_Except      Remove results from the temporary index pDest->iSDParm.
 *
 *     SRT_Exists      Store a 1 in memory cell pDest->iSDParm if the result
 *                     set is not empty.
 *
 *     SRT_Discard     Throw the results away.  This is used by SELECT
 *                     statements within triggers whose only purpose is
 *                     the side-effects of functions.
 *
 * All of the above are free to ignore their ORDER BY clause. Those that
 * follow must honor the ORDER BY clause.
 *
 *     SRT_Output      Generate a row of output (using the OP_ResultRow
 *                     opcode) for each row in the result set.
 *
 *     SRT_Mem         Only valid if the result is a single column.
 *                     Store the first column of the first result row
 *                     in register pDest->iSDParm then abandon the rest
 *                     of the query.  This destination implies "LIMIT 1".
 *
 *     SRT_Set         The result must be a single column.  Store each
 *                     row of result as the key in table pDest->iSDParm.
 *                     Apply the type pDest->det_type before storing
 *                     results.  Used to implement "IN (SELECT ...)".
 *
 *     SRT_EphemTab    Create an temporary table pDest->iSDParm and store
 *                     the result there. The cursor is left open after
 *                     returning.  This is like SRT_Table except that
 *                     this destination uses OP_OpenEphemeral to create
 *                     the table first.
 *
 *     SRT_Coroutine   Generate a co-routine that returns a new row of
 *                     results each time it is invoked.  The entry point
 *                     of the co-routine is stored in register pDest->iSDParm
 *                     and the result row is stored in pDest->nDest registers
 *                     starting with pDest->iSdst.
 *
 *     SRT_Table       Store results in temporary table pDest->iSDParm.
 *     SRT_Fifo        This is like SRT_EphemTab except that the table
 *                     is assumed to already be open.  SRT_Fifo has
 *                     the additional property of being able to ignore
 *                     the ORDER BY clause.
 *
 *     SRT_DistFifo    Store results in a temporary table pDest->iSDParm.
 *                     But also use temporary table pDest->iSDParm+1 as
 *                     a record of all prior results and ignore any duplicate
 *                     rows.  Name means:  "Distinct Fifo".
 *
 *     SRT_Queue       Store results in priority queue pDest->iSDParm (really
 *                     an index).  Append a sequence number so that all entries
 *                     are distinct.
 *
 *     SRT_DistQueue   Store results in priority queue pDest->iSDParm only if
 *                     the same record has never been stored before.  The
 *                     index at pDest->iSDParm+1 hold all prior stores.
 */
#define SRT_Union        1	/* Store result as keys in an index */
#define SRT_Except       2	/* Remove result from a UNION index */
#define SRT_Exists       3	/* Store 1 if the result is not empty */
#define SRT_Discard      4	/* Do not save the results anywhere */
#define SRT_Fifo         5	/* Store result as data with an automatic rowid */
#define SRT_DistFifo     6	/* Like SRT_Fifo, but unique results only */
#define SRT_Queue        7	/* Store result in an queue */
#define SRT_DistQueue    8	/* Like SRT_Queue, but unique results only */

/* The ORDER BY clause is ignored for all of the above */
#define IgnorableOrderby(X) ((X->eDest)<=SRT_DistQueue)

#define SRT_Output       9	/* Output each row of result */
#define SRT_Mem         10	/* Store result in a memory cell */
#define SRT_Set         11	/* Store results as keys in an index */
#define SRT_EphemTab    12	/* Create transient tab and store like SRT_Table */
#define SRT_Coroutine   13	/* Generate a single row of result */
#define SRT_Table       14	/* Store result as data with an automatic rowid */

/*
 * An instance of this object describes where to put of the results of
 * a SELECT statement.
 */
struct SelectDest {
	u8 eDest;		/* How to dispose of the results.  On of SRT_* above. */
	/** Type used when eDest==SRT_Set */
	enum field_type *dest_type;
	int iSDParm;		/* A parameter used by the eDest disposal method */
	/** Register containing ephemeral's space pointer. */
	int reg_eph;
	int iSdst;		/* Base register where results are written */
	int nSdst;		/* Number of registers allocated */
	ExprList *pOrderBy;	/* Key columns for SRT_Queue and SRT_DistQueue */
};

/*
 * Size of the column cache
 */
#ifndef SQL_N_COLCACHE
#define SQL_N_COLCACHE 10
#endif

/*
 * At least one instance of the following structure is created for
 * each trigger that may be fired while parsing an INSERT, UPDATE
 * or DELETE statement. All such objects are stored in the linked
 * list headed at Parse.pTriggerPrg and deleted once statement
 * compilation has been completed.
 *
 * A Vdbe sub-program that implements the body and WHEN clause of
 * trigger TriggerPrg.pTrigger, assuming a default ON CONFLICT
 * clause of TriggerPrg.orconf, is stored in the
 * TriggerPrg.pProgram variable. The Parse.pTriggerPrg list never
 * contains two entries with the same values for both pTrigger
 * and orconf.
 *
 * The TriggerPrg.column_mask[0] variable is set to a mask of
 * old.* columns accessed (or set to 0 for triggers fired as a
 * result of INSERT statements). Similarly, the
 * TriggerPrg.column_mask[1] variable is set to a mask of new.*
 * columns used by the program.
 */
struct TriggerPrg {
	/** Trigger this program was coded from. */
	struct sql_trigger *trigger;
	TriggerPrg *pNext;	/* Next entry in Parse.pTriggerPrg list */
	SubProgram *pProgram;	/* Program implementing pTrigger/orconf */
	int orconf;		/* Default ON CONFLICT policy */
	/* Masks of old.*, new.* columns accessed. */
	uint64_t column_mask[2];
};

enum ast_type {
	AST_TYPE_UNDEFINED = 0,
	AST_TYPE_SELECT,
	AST_TYPE_EXPR,
	AST_TYPE_TRIGGER,
	ast_type_MAX
};

/*
 * An SQL parser context.  A copy of this structure is passed through
 * the parser and down into all the parser action routine in order to
 * carry around information that is global to the entire parse.
 *
 * The structure is divided into two parts.  When the parser and code
 * generate call themselves recursively, the first part of the structure
 * is constant but the second part is reset at the beginning and end of
 * each recursion.
 */
struct Parse {
	sql *db;		/* The main database structure */
	Vdbe *pVdbe;		/* An engine for executing database bytecode */
	u8 colNamesSet;		/* TRUE after OP_ColumnName has been issued to pVdbe */
	u8 nTempReg;		/* Number of temporary registers in aTempReg[] */
	u8 isMultiWrite;	/* True if statement may modify/insert multiple rows */
	u8 hasCompound;		/* Need to invoke convertCompoundSelectToSubquery() */
	u8 okConstFactor;	/* OK to factor out constants */
	u8 disableLookaside;	/* Number of times lookaside has been disabled */
	u8 nColCache;		/* Number of entries in aColCache[] */
	int nRangeReg;		/* Size of the temporary register block */
	int iRangeReg;		/* First register in temporary register block */
	int nTab;		/* Number of previously allocated VDBE cursors */
	int nMem;		/* Number of memory cells used so far */
	int nOpAlloc;		/* Number of slots allocated for Vdbe.aOp[] */
	int szOpAlloc;		/* Bytes of memory space allocated for Vdbe.aOp[] */
	/*
	 * The register with vdbe_field_ref to generate an
	 * alternative Vdbe code (during check constraints).
	 */
	int vdbe_field_ref_reg;
	int iSelfTab;		/* Table of an index whose exprs are being coded */
	int iCacheLevel;	/* ColCache valid when aColCache[].iLevel<=iCacheLevel */
	int iCacheCnt;		/* Counter used to generate aColCache[].lru values */
	int nLabel;		/* Number of labels used */
	int *aLabel;		/* Space to hold the labels */
	ExprList *pConstExpr;	/* Constant expressions */
	int nMaxArg;		/* Max args passed to user function by sub-program */
	int nSelect;		/* Number of SELECT statements seen */
	int nSelectIndent;	/* How far to indent SELECTTRACE() output */
	Parse *pToplevel;	/* Parse structure for main program (or NULL) */
	u32 nQueryLoop;		/* Est number of iterations of a query (10*log2(N)) */
	/* Mask of old.* columns referenced. */
	uint64_t oldmask;
	/* Mask of new.* columns referenced. */
	uint64_t newmask;
	u8 eTriggerOp;		/* TK_UPDATE, TK_INSERT or TK_DELETE */
	u8 eOrconf;		/* Default ON CONFLICT policy for trigger steps */
	/** Region to make SQL temp allocations. */
	struct region region;
	/** True, if error should be raised after parsing. */
	bool is_aborted;

  /**************************************************************************
  * Fields above must be initialized to zero.  The fields that follow,
  * down to the beginning of the recursive section, do not need to be
  * initialized as they will be set before being used.  The boundary is
  * determined by offsetof(Parse,aColCache).
  *************************************************************************/

	struct yColCache {
		int iTable;	/* Table cursor number */
		i16 iColumn;	/* Table column number */
		u8 tempReg;	/* iReg is a temp register that needs to be freed */
		int iLevel;	/* Nesting level */
		int iReg;	/* Reg with value of this column. 0 means none. */
		int lru;	/* Least recently used entry has the smallest value */
	} aColCache[SQL_N_COLCACHE];	/* One for each column cache entry */
	int aTempReg[8];	/* Holding area for temporary registers */

  /************************************************************************
  * Above is constant between recursions.  Below is reset before and after
  * each recursion.  The boundary between these two regions is determined
  * using offsetof(Parse,sLastToken) so the sLastToken field must be the
  * first field in the recursive region.
  ***********************************************************************/

	Token sLastToken;	/* The last token parsed */
	/** The line counter. */
	uint32_t line_count;
	/**
	 * The position in a line. Line and position are used
	 * for detailed error diagnostics.
	 */
	int line_pos;
	ynVar nVar;		/* Number of '?' variables seen in the SQL so far */
	u8 explain;		/* True if the EXPLAIN flag is found on the query */
	int nHeight;		/* Expression tree height of current sub-select */
	int iSelectId;		/* ID of current select for EXPLAIN output */
	int iNextSelectId;	/* Next available select ID for EXPLAIN output */
	VList *pVList;		/* Mapping between variable names and numbers */
	Vdbe *pReprepare;	/* VM being reprepared (sqlReprepare()) */
	const char *zTail;	/* All SQL text past the last semicolon parsed */
	TriggerPrg *pTriggerPrg;	/* Linked list of coded triggers */
	With *pWith;		/* Current WITH clause, or NULL */
	With *pWithToFree;	/* Free this WITH object at the end of the parse */
	/** Index of previous auto generated name. */
	uint32_t autoname_i;
	/** Space triggers are being coded for. */
	struct space *triggered_space;
	/**
	 * One of parse_def structures which are used to
	 * assemble and carry arguments of DDL routines
	 * from parse.y
	 */
	union {
		struct create_ck_def create_ck_def;
		struct create_fk_def create_fk_def;
		struct create_index_def create_index_def;
		struct create_trigger_def create_trigger_def;
		struct create_view_def create_view_def;
		struct rename_entity_def rename_entity_def;
		struct drop_constraint_def drop_constraint_def;
		struct drop_index_def drop_index_def;
		struct drop_table_def drop_table_def;
		struct drop_trigger_def drop_trigger_def;
		struct drop_view_def drop_view_def;
		struct enable_entity_def enable_entity_def;
	};
	/**
	 * Table def or column def is not part of union since
	 * information being held must survive till the end of
	 * parsing of whole <CREATE TABLE> or
	 * <ALTER TABLE ADD COLUMN> statement (to pass it to
	 * sqlEndTable() sql_create_column_end() function).
	 */
	struct create_table_def create_table_def;
	struct create_column_def create_column_def;
	/*
	 * FK and CK constraints appeared in a <CREATE TABLE> or
	 * an <ALTER TABLE ADD COLUMN> statement.
	 */
	struct create_fk_constraint_parse_def create_fk_constraint_parse_def;
	struct create_ck_constraint_parse_def create_ck_constraint_parse_def;
	/*
	 * True, if column in a <CREATE TABLE> or an
	 * <ALTER TABLE ADD COLUMN> statement to be created has
	 * <AUTOINCREMENT>.
	 */
	bool has_autoinc;
	/* Id of field with <AUTOINCREMENT>. */
	uint32_t autoinc_fieldno;
	bool initiateTTrans;	/* Initiate Tarantool transaction */
	/** If set - do not emit byte code at all, just parse.  */
	bool parse_only;
	/** Type of parsed_ast member. */
	enum ast_type parsed_ast_type;
	/** SQL options which were used to compile this VDBE. */
	uint32_t sql_flags;
	/**
	 * Members of this union are valid only
	 * if parse_only is set to true.
	 */
	union {
		struct Expr *expr;
		struct Select *select;
		struct sql_trigger *trigger;
	} parsed_ast;
};

/*
 * Bitfield flags for P5 value in various opcodes.
 *
 * Value constraints (enforced via assert()):
 *    OPFLAG_LENGTHARG    == sql_FUNC_LENGTH
 *    OPFLAG_TYPEOFARG    == sql_FUNC_TYPEOF
 *    OPFLAG_FORDELETE    == BTREE_FORDELETE
 *    OPFLAG_SAVEPOSITION == BTREE_SAVEPOSITION
 *    OPFLAG_AUXDELETE    == BTREE_AUXDELETE
 */
#define OPFLAG_NCHANGE       0x01	/* OP_Insert: Set to update db->nChange */
				     /* Also used in P2 (not P5) of OP_Delete */
#define OPFLAG_EPHEM         0x01	/* OP_Column: Ephemeral output is ok */
#define OPFLAG_OE_IGNORE    0x200	/* OP_IdxInsert: Ignore flag */
#define OPFLAG_OE_FAIL      0x400	/* OP_IdxInsert: Fail flag */
#define OPFLAG_OE_ROLLBACK  0x800	/* OP_IdxInsert: Rollback flag. */
#define OPFLAG_LENGTHARG     0x40	/* OP_Column only used for length() */
#define OPFLAG_TYPEOFARG     0x80	/* OP_Column only used for typeof() */
#define OPFLAG_SEEKEQ        0x02	/* OP_Open** cursor uses EQ seek only */
#define OPFLAG_FORDELETE     0x08	/* OP_Open should use BTREE_FORDELETE */
#define OPFLAG_P2ISREG       0x10	/* P2 to OP_Open** is a register number */
#define OPFLAG_PERMUTE       0x01	/* OP_Compare: use the permutation */
#define OPFLAG_SAVEPOSITION  0x02	/* OP_Delete: keep cursor position */
#define OPFLAG_AUXDELETE     0x04	/* OP_Delete: index in a DELETE op */

#define OPFLAG_SAME_FRAME    0x01	/* OP_FCopy: use same frame for source
					 * register
					 */
#define OPFLAG_NOOP_IF_NULL  0x02	/* OP_FCopy: if source register is NULL
					 * then do nothing
					 */
#define OPFLAG_SYSTEMSP      0x20	/* OP_Open**: set if space pointer
					 * points to system space.
					 */

/**
 * Prepare vdbe P5 flags for OP_{IdxInsert, IdxReplace, Update}
 * by on_conflict action.
 */
#define SET_CONFLICT_FLAG(opflag, on_conflict) do { \
	if (on_conflict == ON_CONFLICT_ACTION_IGNORE) \
	    opflag |= OPFLAG_OE_IGNORE; \
	else if (on_conflict == ON_CONFLICT_ACTION_FAIL) \
	    opflag |= OPFLAG_OE_FAIL; \
	else if (on_conflict == ON_CONFLICT_ACTION_ROLLBACK) \
	    opflag |= OPFLAG_OE_ROLLBACK; \
} while (0)

/* OP_RowData: xferOptimization started processing */
#ifdef SQL_TEST
#define OPFLAG_XFER_OPT      0x01
#endif

/*
 * Each trigger present in the database schema is stored as an
 * instance of struct sql_trigger.
 * Pointers to instances of struct sql_trigger are stored in a
 * linked list, using the next member of struct sql_trigger. A
 * pointer to the first element of the linked list is stored as
 * sql_triggers member of the associated space.
 *
 * The "step_list" member points to the first element of a linked
 * list containing the SQL statements specified as the trigger
 * program.
 */
struct sql_trigger {
	/** The name of the trigger. */
	char *zName;
	/** The ID of space the trigger refers to. */
	uint32_t space_id;
	/** One of TK_DELETE, TK_UPDATE, TK_INSERT. */
	u8 op;
	/** One of TRIGGER_BEFORE, TRIGGER_AFTER. */
	u8 tr_tm;
	/** The WHEN clause of the expression (may be NULL). */
	Expr *pWhen;
	/**
	 * If this is an UPDATE OF <column-list> trigger,
	 * the <column-list> is stored here
	 */
	IdList *pColumns;
	/** Link list of trigger program steps. */
	TriggerStep *step_list;
	/** Next trigger associated with the table. */
	struct sql_trigger *next;
};

/*
 * A trigger is either a BEFORE or an AFTER trigger.  The following constants
 * determine which.
 *
 * If there are multiple triggers, you might of some BEFORE and some AFTER.
 * In that cases, the constants below can be ORed together.
 */
#define TRIGGER_BEFORE  1
#define TRIGGER_AFTER   2

/*
 * An instance of struct TriggerStep is used to store a single SQL statement
 * that is a part of a trigger-program.
 *
 * Instances of struct TriggerStep are stored in a singly linked list (linked
 * using the "pNext" member) referenced by the "step_list" member of the
 * associated struct sql_trigger instance. The first element of the linked list
 * is the first step of the trigger-program.
 *
 * The "op" member indicates whether this is a "DELETE", "INSERT", "UPDATE" or
 * "SELECT" statement. The meanings of the other members is determined by the
 * value of "op" as follows:
 *
 * (op == TK_INSERT)
 * orconf    -> stores the ON CONFLICT algorithm
 * pSelect   -> If this is an INSERT INTO ... SELECT ... statement, then
 *              this stores a pointer to the SELECT statement. Otherwise NULL.
 * zTarget   -> Dequoted name of the table to insert into.
 * pExprList -> If this is an INSERT INTO ... VALUES ... statement, then
 *              this stores values to be inserted. Otherwise NULL.
 * pIdList   -> If this is an INSERT INTO ... (<column-names>) VALUES ...
 *              statement, then this stores the column-names to be
 *              inserted into.
 *
 * (op == TK_DELETE)
 * zTarget   -> Dequoted name of the table to delete from.
 * pWhere    -> The WHERE clause of the DELETE statement if one is specified.
 *              Otherwise NULL.
 *
 * (op == TK_UPDATE)
 * zTarget   -> Dequoted name of the table to update.
 * pWhere    -> The WHERE clause of the UPDATE statement if one is specified.
 *              Otherwise NULL.
 * pExprList -> A list of the columns to update and the expressions to update
 *              them to. See sqlUpdate() documentation of "pChanges"
 *              argument.
 *
 */
struct TriggerStep {
	u8 op;			/* One of TK_DELETE, TK_UPDATE, TK_INSERT, TK_SELECT */
	u8 orconf;		/* ON_CONFLICT_ACTION_ROLLBACK etc. */
	Select *pSelect;	/* SELECT statement or RHS of INSERT INTO SELECT ... */
	char *zTarget;		/* Target table for DELETE, UPDATE, INSERT */
	Expr *pWhere;		/* The WHERE clause for DELETE or UPDATE steps */
	ExprList *pExprList;	/* SET clause for UPDATE. */
	IdList *pIdList;	/* Column names for INSERT */
	TriggerStep *pNext;	/* Next in the link-list */
	TriggerStep *pLast;	/* Last element in link-list. Valid for 1st elem only */
};

/*
 * An objected used to accumulate the text of a string where we
 * do not necessarily know how big the string will be in the end.
 */
struct StrAccum {
	sql *db;		/* Optional database for lookaside.  Can be NULL */
	char *zBase;		/* A base allocation.  Not from malloc. */
	char *zText;		/* The string collected so far */
	u32 nChar;		/* Length of the string so far */
	u32 nAlloc;		/* Amount of space allocated in zText */
	u32 mxAlloc;		/* Maximum allowed allocation.  0 for no malloc usage */
	u8 accError;		/* STRACCUM_NOMEM or STRACCUM_TOOBIG */
	u8 printfFlags;		/* sql_PRINTF flags below */
};
#define STRACCUM_NOMEM   1
#define STRACCUM_TOOBIG  2
#define SQL_PRINTF_INTERNAL 0x01	/* Internal-use-only converters allowed */
#define SQL_PRINTF_SQLFUNC  0x02	/* SQL function arguments to VXPrintf */
#define SQL_PRINTF_MALLOCED 0x04	/* True if xText is allocated space */

#define isMalloced(X)  (((X)->printfFlags & SQL_PRINTF_MALLOCED)!=0)

/*
 * Structure containing global configuration data for the sql library.
 *
 * This structure also contains some state information.
 */
struct sqlConfig {
	sql_int64 szMmap;	/* mmap() space per open file */
	sql_int64 mxMmap;	/* Maximum value for szMmap */
	u32 szPma;		/* Maximum Sorter PMA size */
	/* The above might be initialized to non-zero.  The following need to always
	 * initially be zero, however.
	 */
	int isInit;		/* True after initialization has finished */
	int inProgress;		/* True while initialization in progress */
#ifdef SQL_VDBE_COVERAGE
	/* The following callback (if not NULL) is invoked on every VDBE branch
	 * operation.  Set the callback using sql_TESTCTRL_VDBE_COVERAGE.
	 */
	void (*xVdbeBranch) (void *, int iSrcLine, u8 eThis, u8 eMx);	/* Callback */
	void *pVdbeBranchArg;	/* 1st argument */
#endif
	int iOnceResetThreshold;	/* When to reset OP_Once counters */
};

/*
 * Context pointer passed down through the tree-walk.
 */
struct Walker {
	Parse *pParse;		/* Parser context.  */
	int (*xExprCallback) (Walker *, Expr *);	/* Callback for expressions */
	int (*xSelectCallback) (Walker *, Select *);	/* Callback for SELECTs */
	void (*xSelectCallback2) (Walker *, Select *);	/* Second callback for SELECTs */
	int walkerDepth;	/* Number of subqueries */
	u8 eCode;		/* A small processing code */
	union {			/* Extra data for callback */
		NameContext *pNC;	/* Naming context */
		int n;		/* A counter */
		int iCur;	/* A cursor number */
		SrcList *pSrcList;	/* FROM clause */
		struct SrcCount *pSrcCount;	/* Counting column references */
		int *aiCol;	/* array of column indexes */
		/** Space definition. */
		struct space_def *space_def;
	} u;
};

/* Forward declarations */
int sqlWalkExpr(Walker *, Expr *);
int sqlWalkExprList(Walker *, ExprList *);
int sqlWalkSelect(Walker *, Select *);
int sqlWalkSelectExpr(Walker *, Select *);
int sqlWalkSelectFrom(Walker *, Select *);
int sqlExprWalkNoop(Walker *, Expr *);

/*
 * Return code from the parse-tree walking primitives and their
 * callbacks.
 */
#define WRC_Continue    0	/* Continue down into children */
#define WRC_Prune       1	/* Omit children but continue walking siblings */
#define WRC_Abort       2	/* Abandon the tree walk */

/*
 * An instance of this structure represents a set of one or more CTEs
 * (common table expressions) created by a single WITH clause.
 */
struct With {
	int nCte;		/* Number of CTEs in the WITH clause */
	With *pOuter;		/* Containing WITH clause, or NULL */
	struct Cte {		/* For each CTE in the WITH clause.... */
		char *zName;	/* Name of this CTE */
		ExprList *pCols;	/* List of explicit column names, or NULL */
		Select *pSelect;	/* The definition of this CTE */
		const char *zCteErr;	/* Error message for circular references */
	} a[1];
};

#ifdef SQL_DEBUG
/*
 * An instance of the TreeView object is used for printing the content of
 * data structures on sqlDebugPrintf() using a tree-like view.
 */
struct TreeView {
	int iLevel;		/* Which level of the tree we are on */
	u8 bLine[100];		/* Draw vertical in column i if bLine[i] is true */
};
#endif				/* SQL_DEBUG */

/*
 * The following macros mimic the standard library functions toupper(),
 * isspace(), isalnum(), isdigit() and isxdigit(), respectively. The
 * sql versions only work for ASCII characters, regardless of locale.
 */
#define sqlToupper(x)  ((x)&~(sqlCtypeMap[(unsigned char)(x)]&0x20))
#define sqlIsspace(x)   (sqlCtypeMap[(unsigned char)(x)]&0x01)
#define sqlIsalnum(x)   (sqlCtypeMap[(unsigned char)(x)]&0x06)
#define sqlIsalpha(x)   (sqlCtypeMap[(unsigned char)(x)]&0x02)
#define sqlIsdigit(x)   (sqlCtypeMap[(unsigned char)(x)]&0x04)
#define sqlIsxdigit(x)  (sqlCtypeMap[(unsigned char)(x)]&0x08)
#define sqlTolower(x)   (sqlUpperToLower[(unsigned char)(x)])
#define sqlIsquote(x)   (sqlCtypeMap[(unsigned char)(x)]&0x80)

/*
 * Internal function prototypes
 */
int sqlStrICmp(const char *, const char *);
unsigned sqlStrlen30(const char *);
#define sqlStrNICmp sql_strnicmp

void *sqlMalloc(u64);
void *sqlMallocZero(u64);
void *sqlDbMallocZero(sql *, u64);
void *sqlDbMallocRaw(sql *, u64);
void *sqlDbMallocRawNN(sql *, u64);
char *sqlDbStrDup(sql *, const char *);
char *sqlDbStrNDup(sql *, const char *, u64);
void *sqlRealloc(void *, u64);
void *sqlDbReallocOrFree(sql *, void *, u64);
void *sqlDbRealloc(sql *, void *, u64);
void sqlDbFree(sql *, void *);
int sqlMallocSize(void *);
int sqlDbMallocSize(sql *, void *);

/*
 * On systems with ample stack space and that support alloca(), make
 * use of alloca() to obtain space for large automatic objects.  By default,
 * obtain space from malloc().
 *
 * The alloca() routine never returns NULL.  This will cause code paths
 * that deal with sqlStackAlloc() failures to be unreachable.
 */
#ifdef SQL_USE_ALLOCA
#define sqlStackAllocRaw(D,N)   alloca(N)
#define sqlStackAllocZero(D,N)  memset(alloca(N), 0, N)
#define sqlStackFree(D,P)
#else
#define sqlStackAllocRaw(D,N)   sqlDbMallocRaw(D,N)
#define sqlStackAllocZero(D,N)  sqlDbMallocZero(D,N)
#define sqlStackFree(D,P)       sqlDbFree(D,P)
#endif

int sqlIsNaN(double);

/*
 * An instance of the following structure holds information about SQL
 * functions arguments that are the parameters to the printf() function.
 */
struct PrintfArguments {
	int nArg;		/* Total number of arguments */
	int nUsed;		/* Number of arguments used so far */
	sql_value **apArg;	/* The argument values */
};

void sqlVXPrintf(StrAccum *, const char *, va_list);
void sqlXPrintf(StrAccum *, const char *, ...);
char *sqlMPrintf(sql *, const char *, ...);
char *sqlVMPrintf(sql *, const char *, va_list);
#if defined(SQL_DEBUG)
void sqlDebugPrintf(const char *, ...);
#endif
#if defined(SQL_TEST)
void *sqlTestTextToPtr(const char *);
#endif

#if defined(SQL_DEBUG)
void sqlTreeViewExpr(TreeView *, const Expr *, u8);
void sqlTreeViewBareExprList(TreeView *, const ExprList *, const char *);
void sqlTreeViewExprList(TreeView *, const ExprList *, u8, const char *);
void sqlTreeViewSelect(TreeView *, const Select *, u8);
void sqlTreeViewWith(TreeView *, const With *);
#endif

void sqlDequote(char *);

/**
 * Perform SQL name normalization: cast name to the upper-case
 * (via Unicode Character Folding). Casing is locale-independent
 * and context-sensitive. The result may be longer or shorter
 * than the original. The source string and the destination buffer
 * must not overlap.
 * For example, ß is converted to SS.
 * The result is similar to SQL UPPER function.
 *
 * @param dst A buffer for the result string. The result will be
 *        0-terminated if the buffer is large enough. The contents
 *        is undefined in case of failure.
 * @param dst_size The size of the buffer (number of bytes).
 * @param src The original string.
 * @param src_len The length of the original string.
 * @retval The count of bytes written (or need to be written).
 */
int
sql_normalize_name(char *dst, int dst_size, const char *src, int src_len);

/**
 * Duplicate a normalized version of @a name onto an sqlMalloc.
 * For normalization rules @sa sql_normalize_name().
 * @param db SQL context.
 * @param name Source string.
 * @param len Length of @a name.
 * @retval Not NULL Success. A normalized string is returned.
 * @retval NULL Error. A diag message is set.
 */
char *
sql_normalized_name_db_new(struct sql *db, const char *name, int len);

/**
 * Duplicate a normalized version of @a name onto a region @a r.
 * For normalization rules @sa sql_normalize_name().
 * @param r Region allocator.
 * @param name Source string.
 * @param len Length of @a name.
 * @retval Not NULL Success. A normalized string is returned.
 * @retval NULL Error. A diag message is set. Region is not
 *         truncated back.
 */
char *
sql_normalized_name_region_new(struct region *r, const char *name, int len);

int sqlKeywordCode(const unsigned char *, int);
int sqlRunParser(Parse *, const char *);

/**
 * This routine is called after a single SQL statement has been
 * parsed and a VDBE program to execute that statement has been
 * prepared.  This routine puts the finishing touches on the
 * VDBE program and resets the pParse structure for the next
 * parse.
 *
 * Note that if an error occurred, it might be the case that
 * no VDBE code was generated.
 *
 * @param parse_context Current parsing context.
 */
void
sql_finish_coding(struct Parse *parse_context);

int sqlGetTempReg(Parse *);
void sqlReleaseTempReg(Parse *, int);
int sqlGetTempRange(Parse *, int);
void sqlReleaseTempRange(Parse *, int, int);
void sqlClearTempRegCache(Parse *);

/**
 * Construct a new expression. Memory for this node and for the
 * token argument is a single allocation obtained from
 * sqlDbMalloc(). The calling function is responsible for making
 * sure the node eventually gets freed.
 *
 * Special case: If op==TK_INTEGER and token points to a string
 * that can be translated into a 32-bit integer, then the token is
 * not stored in u.zToken. Instead, the integer values is written
 * into u.iValue and the EP_IntValue flag is set. No extra storage
 * is allocated to hold the integer text.
 *
 * @param db The database connection.
 * @param op Expression opcode (TK_*).
 * @param token Source token. Might be NULL.
 * @retval Not NULL New expression object on success.
 * @retval NULL Otherwise. The diag message is set.
 */
struct Expr *
sql_expr_new(struct sql *db, int op, const struct Token *token);

/**
 * The same as @sa sql_expr_new, but normalizes name, stored in
 * @a token. Quotes are removed if they are presented.
 */
struct Expr *
sql_expr_new_dequoted(struct sql *db, int op, const struct Token *token);

/**
 * The same as @a sql_expr_new, but takes const char instead of
 * Token. Just sugar to do not touch tokens in many places.
 */
static inline struct Expr *
sql_expr_new_named(struct sql *db, int op, const char *name)
{
	struct Token name_token;
	sqlTokenInit(&name_token, (char *)name);
	return sql_expr_new(db, op, &name_token);
}

/**
 * The same as @a sql_expr_new, but a result expression has no
 * name.
 */
static inline struct Expr *
sql_expr_new_anon(struct sql *db, int op)
{
	return sql_expr_new_named(db, op, NULL);
}

void sqlExprAttachSubtrees(sql *, Expr *, Expr *, Expr *);
Expr *sqlPExpr(Parse *, int, Expr *, Expr *);
void sqlPExprAddSelect(Parse *, Expr *, Select *);

/**
 * Join two expressions using an AND operator. If either
 * expression is NULL, then just return the other expression.
 *
 * If one side or the other of the AND is known to be false, then
 * instead of returning an AND expression, just return a constant
 * expression with a value of false.
 *
 * @param db The database connection.
 * @param left_expr The left-branch expresion to join.
 * @param right_expr The right-branch expression to join.
 * @retval Not NULL New expression root node pointer on success.
 * @retval NULL Error. A diag message is set.
 * @retval NULL Not an error. Both arguments were NULL.
 */
struct Expr *
sql_and_expr_new(struct sql *db, struct Expr *left_expr,
		 struct Expr *right_expr);

Expr *sqlExprFunction(Parse *, ExprList *, Token *);
void sqlExprAssignVarNumber(Parse *, Expr *, u32);
ExprList *sqlExprListAppendVector(Parse *, ExprList *, IdList *, Expr *);

/**
 * Set the sort order for the last element on the given ExprList.
 *
 * @param p Expression list.
 * @param sort_order Sort order to set.
 */
void sqlExprListSetSortOrder(ExprList *, enum sort_order sort_order);

/**
 * Check if sorting orders are the same in ORDER BY and raise an
 * error if they are not.
 *
 * This check is needed only for ORDER BY + LIMIT, because
 * currently ORDER BY + LIMIT + ASC + DESC produces incorrectly
 * sorted results and thus forbidden. In future, we will
 * support different sorting orders in
 * ORDER BY + LIMIT (e.g. ORDER BY col1 ASC, col2 DESC LIMIT ...)
 * and remove this check.
 * @param parse Parsing context.
 * @param expr_list Expression list with  ORDER BY clause
 * at the end.
 */
void
sql_expr_check_sort_orders(struct Parse *parse,
			   const struct ExprList *expr_list);

void sqlExprListSetName(Parse *, ExprList *, Token *, int);
void sqlExprListSetSpan(Parse *, ExprList *, ExprSpan *);
u32 sqlExprListFlags(const ExprList *);
int sqlInit(sql *);

/*
 * Process a pragma statement.
 *
 * Pragmas are of this form:
 * PRAGMA <pragma_name>;
 * PRAGMA <pragma_name>(<table_name>);
 * PRAGMA <pragma_name>(<table_name>.<index_name>);
 *
 * @param pParse Parse context.
 * @param pragma Name of the pragma.
 * @param table Name of the table.
 * @param index Name of the index.
 */
void
sqlPragma(struct Parse *pParse, struct Token *pragma, struct Token *table,
	  struct Token *index);

/**
 * Return true if given column is part of primary key.
 * If field number is less than 63, corresponding bit
 * in column mask is tested. Otherwise, check whether 64-th bit
 * in mask is set or not. If it is set, then iterate through
 * key parts of primary index and check field number.
 * In case it isn't set, there are no key columns among
 * the rest of fields.
 */
bool
sql_space_column_is_in_pk(struct space *space, uint32_t);

/**
 * Given an expression list (which is really the list of expressions
 * that form the result set of a SELECT statement) compute appropriate
 * column names for a table that would hold the expression list.
 * All column names will be unique.
 * Initialize fields and field_count.
 *
 * @param parse Parsing context.
 * @param expr_list  Expr list from which to derive column names.
 * @param space_def Destination space definition.
 * @retval 0 on success.
 * @retval error codef on error.
 */
int sqlColumnsFromExprList(Parse *parse, ExprList *expr_list,
			   struct space_def *space_def);

void
sqlSelectAddColumnTypeAndCollation(Parse *, struct space_def *, Select *);
struct space *sqlResultSetOfSelect(Parse *, Select *);

struct space *
sqlStartTable(Parse *, Token *);

/**
 * Add new field to the format of ephemeral space in
 * create_column_def. If it is <ALTER TABLE> create shallow copy
 * of the existing space and add field to its format.
 */
void
sql_create_column_start(struct Parse *parse);

/**
 * Emit code to update entry in _space and code to create
 * constraints (entries in _index, _ck_constraint, _fk_constraint)
 * described with this column.
 */
void
sql_create_column_end(struct Parse *parse);

/**
 * This routine is called by the parser while in the middle of
 * parsing a <CREATE TABLE> or a <ALTER TABLE ADD COLUMN>
 * statement. A "NOT NULL" constraint has been seen on a column.
 * This routine sets the is_nullable flag on the column currently
 * under construction. If nullable_action has been already set,
 * this function raises an error.
 *
 * @param parser SQL Parser object.
 * @param nullable_action on_conflict_action value.
 */
void
sql_column_add_nullable_action(struct Parse *parser,
			       enum on_conflict_action nullable_action);

void
sqlAddPrimaryKey(struct Parse *parse);

/**
 * Add a new CHECK constraint to the table currently under
 * construction.
 * @param parser Parsing context.
 */
void
sql_create_check_contraint(Parse *parser);

void sqlAddDefaultValue(Parse *, ExprSpan *);
void sqlAddCollateType(Parse *, Token *);

/**
 * Return collation of given column from table.
 * @param def space definition which is used to fetch column.
 * @param column Number of column.
 * @param[out] coll_id Collation identifier.
 *
 * @retval Pointer to collation.
 */
struct coll *
sql_column_collation(struct space_def *def, uint32_t column, uint32_t *coll_id);

void
sqlEndTable(struct Parse *parse);

/**
 * Create cursor which will be positioned to the space/index.
 * It makes space lookup and loads pointer to it into register,
 * which is passes to OP_IteratorOpen as an argument.
 *
 * @param parse_context Parse context.
 * @param cursor Number of cursor to be created.
 * @param index_id index id. In future will be replaced with
 *        pointer to struct index.
 * @param space Pointer to space object.
 * @retval address of last opcode.
 */
int
vdbe_emit_open_cursor(struct Parse *parse, int cursor, int index_id,
		      struct space *space);

/**
 * The parser calls this routine in order to create a new VIEW.
 *
 * @param parse_context Current parsing context.
 */
void
sql_create_view(struct Parse *parse_context);

/**
 * Compile view, i.e. create struct Select from
 * 'CREATE VIEW...' string, and assign cursors to each table from
 * 'FROM' clause.
 *
 * @param parse Parsing context.
 * @param view_stmt String containing 'CREATE VIEW' statement.
 * @retval 0 if success, -1 in case of error.
 */
int
sql_view_assign_cursors(struct Parse *parse, const char *view_stmt);

/**
 * Store duplicate of SELECT into parsing context.
 * This routine is called during parsing.
 *
 * @param parse_context Current parsing context.
 * @param select Select to be stored.
 */
void
sql_store_select(struct Parse *parse_context, struct Select *select);

void
sql_drop_table(struct Parse *);
void sqlInsert(Parse *, SrcList *, Select *, IdList *,
	       enum on_conflict_action);
void *sqlArrayAllocate(sql *, void *, int, int *, int *);

/**
 * Append a new element to the given IdList. Create a new IdList
 * if need be.
 *
 * @param db The database connection.
 * @param list The pointer to existent Id list if exists.
 * @param name_token The token containing name.
 * @retval Not NULL A new list or updated @a list.
 * @retval NULL Error. Diag message is set.
 */
struct IdList *
sql_id_list_append(struct sql *db, struct IdList *list,
		   struct Token *name_token);

int sqlIdListIndex(IdList *, const char *);

/**
 * Expand the space allocated for the given SrcList object by
 * creating new_slots new slots beginning at start_idx.
 * The start_idx is zero based. New slots are zeroed.
 *
 * For example, suppose a SrcList initially contains two entries:
 * A,B.
 * To append 3 new entries onto the end, do this:
 *    sql_src_list_enlarge(db, src_list, 3, 2);
 *
 * After the call above it would contain:  A, B, nil, nil, nil.
 * If the start_idx argument had been 1 instead of 2, then the
 * result would have been: A, nil, nil, nil, B.  To prepend the
 * new slots, the start_idx value would be 0. The result then
 * would be: nil, nil, nil, A, B.
 *
 * @param db The database connection.
 * @param src_list The SrcList to be enlarged.
 * @param new_slots Number of new slots to add to src_list->a[].
 * @param start_idx Index in src_list->a[] of first new slot.
 * @retval Not NULL SrcList pointer on success.
 * @retval NULL Otherwise. The diag message is set.
 */
struct SrcList *
sql_src_list_enlarge(struct sql *db, struct SrcList *src_list, int new_slots,
		     int start_idx);

/**
 * Allocate a new empty SrcList object.
 *
 * @param db The database connection.
 * @retval Not NULL List pointer on success.
 * @retval NULL Otherwise. The diag message is set.
 */
struct SrcList *
sql_src_list_new(struct sql *db);

/**
 * Append a new table name to the given list. Create a new
 * SrcList if need be. A new entry is created in the list even
 * if name_token is NULL.
 *
 * @param db The database connection.
 * @param list Append to this SrcList. NULL creates a new SrcList.
 * @param name_token Token representing table name.
 * @retval Not NULL A new SrcList or updated @a list.
 * @retval NULL Error. A diag message is set. @A list is deleted.
 */
struct SrcList *
sql_src_list_append(struct sql *db, struct SrcList *list,
		    struct Token *name_token);

SrcList *sqlSrcListAppendFromTerm(Parse *, SrcList *, Token *,
				      Token *, Select *, Expr *, IdList *);
void sqlSrcListIndexedBy(Parse *, SrcList *, Token *);
void sqlSrcListFuncArgs(Parse *, SrcList *, ExprList *);
int sqlIndexedByLookup(Parse *, struct SrcList_item *);
void sqlSrcListShiftJoinType(SrcList *);
void sqlSrcListAssignCursors(Parse *, SrcList *);
void sqlIdListDelete(sql *, IdList *);

/**
 * Create a new index for an SQL table.  name is the name of the
 * index and tbl_name is the name of the table that is to be
 * indexed.  Both will be NULL for a primary key or an index that
 * is created to satisfy a UNIQUE constraint.  If tbl_name and
 * name are NULL, use parse->new_space as the table to be indexed.
 * parse->create_tale_def->new_space is a space that is currently
 * being constructed by a CREATE TABLE statement.
 *
 * @param parse All information about this parse.
 */
void
sql_create_index(struct Parse *parse);

/**
 * This routine will drop an existing named index.  This routine
 * implements the DROP INDEX statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_drop_index(struct Parse *parse_context);

int sqlSelect(Parse *, Select *, SelectDest *);
Select *sqlSelectNew(Parse *, ExprList *, SrcList *, Expr *, ExprList *,
			 Expr *, ExprList *, u32, Expr *, Expr *);

/**
 * While a SrcList can in general represent multiple spaces and
 * subqueries (as in the FROM clause of a SELECT statement) in
 * this case it contains the name of a single table, as one might
 * find in an INSERT, DELETE, or UPDATE statement. Look up that
 * space in the cache.
 * Set an error message and return NULL if the table name is not
 * found or if space doesn't have format.
 *
 * The following fields are initialized appropriate in src_list:
 *
 *    src_list->a[0].space      Pointer to the space object.
 *    src_list->a[0].pIndex     Pointer to the INDEXED BY index,
 *                              if there is one.
 *
 * @param parse Parsing context.
 * @param space_name Space element.
 * @retval Space object if found, NULL otherwise.
 */
struct space *
sql_lookup_space(struct Parse *parse, struct SrcList_item *space_name);

/**
 * Generate code for a DELETE FROM statement.
 *
 *     DELETE FROM table_wxyz WHERE a<5 AND b NOT NULL;
 *                 \________/       \________________/
 *                  tab_list              where
 *
 * @param parse Parsing context.
 * @param tab_list List of single element which table from which
 * deletetion if performed.
 * @param where The WHERE clause.  May be NULL.
 */
void
sql_table_delete_from(struct Parse *parse, struct SrcList *tab_list,
		      struct Expr *where);

/**
 * Generate a code for TRUNCATE TABLE statement.
 *
 * @param parse Parsing context.
 * @param tab_list List of single table to truncate.
 */
void
sql_table_truncate(struct Parse *parse, struct SrcList *tab_list);

void sqlUpdate(Parse *, SrcList *, ExprList *, Expr *,
		   enum on_conflict_action);
WhereInfo *sqlWhereBegin(Parse *, SrcList *, Expr *, ExprList *, ExprList *,
			     u16, int);
void sqlWhereEnd(WhereInfo *);
LogEst sqlWhereOutputRowCount(WhereInfo *);
int sqlWhereIsDistinct(WhereInfo *);
int sqlWhereIsOrdered(WhereInfo *);
int sqlWhereOrderedInnerLoop(WhereInfo *);
int sqlWhereIsSorted(WhereInfo *);
int sqlWhereContinueLabel(WhereInfo *);
int sqlWhereBreakLabel(WhereInfo *);
int sqlWhereOkOnePass(WhereInfo *, int *);
#define ONEPASS_OFF      0	/* Use of ONEPASS not allowed */
#define ONEPASS_SINGLE   1	/* ONEPASS valid for a single row update */
#define ONEPASS_MULTI    2	/* ONEPASS is valid for multiple rows */

/**
 * Generate code that will extract the iColumn-th column from
 * table pTab and store the column value in a register.
 *
 * An effort is made to store the column value in register iReg.
 * This is not garanteeed for GetColumn() - the result can be
 * stored in any register.  But the result is guaranteed to land
 * in register iReg for GetColumnToReg().
 * @param pParse Parsing and code generating context.
 * @param iColumn Index of the table column.
 * @param iTable The cursor pointing to the table.
 * @param iReg Store results here.
 * @param p5 P5 value for OP_Column + FLAGS.
 * @return iReg value.
 */
int
sqlExprCodeGetColumn(Parse *, int, int, int, u8);

/**
 * Generate code that will extract the iColumn-th column from
 * table defined by space_def and store the column value in
 * a register, copy the result.
 * @param pParse Parsing and code generating context.
 * @param iColumn Index of the table column.
 * @param iTable The cursor pointing to the table.
 * @param iReg Store results here.
 */
void
sqlExprCodeGetColumnToReg(Parse *, int, int, int);

void sqlExprCodeMove(Parse *, int, int, int);
void sqlExprCacheStore(Parse *, int, int, int);
void sqlExprCachePush(Parse *);
void sqlExprCachePop(Parse *);
void sqlExprCacheRemove(Parse *, int, int);
void sqlExprCacheClear(Parse *);
void sql_expr_type_cache_change(Parse *, int, int);
void sqlExprCode(Parse *, Expr *, int);
void sqlExprCodeFactorable(Parse *, Expr *, int);
void sqlExprCodeAtInit(Parse *, Expr *, int, u8);
int sqlExprCodeTemp(Parse *, Expr *, int *);
int sqlExprCodeTarget(Parse *, Expr *, int);
void sqlExprCodeAndCache(Parse *, Expr *, int);
int sqlExprCodeExprList(Parse *, ExprList *, int, int, u8);
#define SQL_ECEL_DUP      0x01	/* Deep, not shallow copies */
#define SQL_ECEL_FACTOR   0x02	/* Factor out constant terms */
#define SQL_ECEL_REF      0x04	/* Use ExprList.u.x.iOrderByCol */
#define SQL_ECEL_OMITREF  0x08	/* Omit if ExprList.u.x.iOrderByCol */
void sqlExprIfTrue(Parse *, Expr *, int, int);
void sqlExprIfFalse(Parse *, Expr *, int, int);

/**
 * Given a token, return a string that consists of the text of
 * that token. Space to hold the returned string is obtained
 * from sqlMalloc() and must be freed by the calling function.
 *
 * Any quotation marks (ex:  "name", 'name', [name], or `name`)
 * that surround the body of the token are removed.
 *
 * Tokens are often just pointers into the original SQL text and
 * so are not \000 terminated and are not persistent. The returned
 * string is \000 terminated and is persistent.
 *
 * @param db The database connection.
 * @param t The source token with text.
 * @retval Not NULL Formatted name on new memory.
 * @retval NULL Error. Diag message is set.
 */
static inline char *
sql_name_from_token(struct sql *db, struct Token *t)
{
	assert(t != NULL && t->z != NULL);
	return sql_normalized_name_db_new(db, t->z, t->n);
}

int sqlExprCompare(Expr *, Expr *, int);
int sqlExprListCompare(ExprList *, ExprList *, int);
int sqlExprImpliesExpr(Expr *, Expr *, int);
void sqlExprAnalyzeAggregates(NameContext *, Expr *);
void sqlExprAnalyzeAggList(NameContext *, ExprList *);
int sqlFunctionUsesThisSrc(Expr *, SrcList *);
Vdbe *sqlGetVdbe(Parse *);
void sqlRollbackAll(Vdbe *);

/**
 * Generate opcodes which start new Tarantool transaction.
 * Used from parser to process BEGIN statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_transaction_begin(struct Parse *parse_context);

/**
 * Generate opcodes which commit Tarantool transaction.
 * Used from parser to process COMMIT statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_transaction_commit(struct Parse *parse_context);

/**
 * Generate opcodes which rollback Tarantool transaction.
 * Used from parser to process ROLLBACK statement.
 *
 * @param parse_context Current parsing context.
 */
void
sql_transaction_rollback(struct Parse *parse_context);

void sqlSavepoint(Parse *, int, Token *);
void sqlCloseSavepoints(Vdbe *);
int sqlExprIsConstant(Expr *);
int sqlExprIsConstantNotJoin(Expr *);
int sqlExprIsConstantOrFunction(Expr *, u8);
int sqlExprIsTableConstant(Expr *, int);
int sqlExprIsInteger(Expr *, int *);
int sqlExprCanBeNull(const Expr *);

/**
 * Return TRUE if the given expression is a constant which would
 * be unchanged by OP_ApplyType with the type given in the second
 * argument.
 *
 * This routine is used to determine if the OP_ApplyType operation
 * can be omitted.  When in doubt return FALSE.  A false negative
 * is harmless. A false positive, however, can result in the wrong
 * answer.
 */
bool
sql_expr_needs_no_type_change(const struct Expr *expr, enum field_type type);

/**
 * This routine generates VDBE code that causes a single row of a
 * single table to be deleted.  Both the original table entry and
 * all indices are removed.
 *
 * Preconditions:
 *
 *   1.  cursor is an open cursor on the btree that is the
 *       canonical data store for the table.  (This will be the
 *       PRIMARY KEY index)
 *
 *   2.  The primary key for the row to be deleted must be stored
 *       in a sequence of npk memory cells starting at reg_pk. If
 *       npk==0 that means that a search record formed from
 *       OP_MakeRecord is contained in the single memory location
 *       reg_pk.
 *
 *   Parameter mode may be passed either ONEPASS_OFF (0),
 *   ONEPASS_SINGLE, or ONEPASS_MULTI.  If mode is not
 *   ONEPASS_OFF, then the cursor already points to the row to
 *   delete. If mode is ONEPASS_OFF then this function must seek
 *   cursor to the entry identified by reg_pk and npk before
 *   reading from it.
 *
 *   If mode is ONEPASS_MULTI, then this call is being made as
 *   part of a ONEPASS delete that affects multiple rows. In this
 *   case, if idx_noseek is a valid cursor number (>=0), then its
 *   position should be preserved following the delete operation.
 *   Or, if idx_noseek is not a valid cursor number, the position
 *   of cursor should be preserved instead.
 *
 * @param parse Parsing context.
 * @param space Space containing the row to be deleted.
 * @param trigger_list List of triggers to (potentially) fire.
 * @param cursor Cursor from which column data is extracted/
 * @param reg_pk First memory cell containing the PRIMARY KEY.
 * @param npk umber of PRIMARY KEY memory cells.
 * @param need_update_count. If non-zero, increment the row change
 *        counter.
 * @param onconf Default ON CONFLICT policy for triggers.
 * @param mode ONEPASS_OFF, _SINGLE, or _MULTI.  See above.
 * @param idx_noseek If it is a valid cursor number (>=0),
 *        then it identifies an index cursor that already points
 *        to the index entry to be deleted.
 */
void
sql_generate_row_delete(struct Parse *parse, struct space *space,
			struct sql_trigger *trigger_list, int cursor,
			int reg_pk, short npk, bool need_update_count,
			enum on_conflict_action onconf, u8 mode,
			int idx_noseek);

/**
 * Generate code to do constraint checks prior to an INSERT or
 * an UPDATE on the given table.
 *
 * The @new_tuple_reg is the first register in a range that
 * contains the data to be inserted or the data after the update.
 * There will be field_count registers in this range.
 * The first register in the range will contains the content of
 * the first table column, and so forth.
 *
 * To test NULL, CHECK and statement (except for REPLACE)
 * constraints we can avoid opening cursors on secondary indexes.
 * However, to implement INSERT OR REPLACE or UPDATE OR REPLACE,
 * we should
 *
 * Constraint
 *    type       Action              What Happens
 * ----------  ----------  --------------------------------------
 *    any       ROLLBACK   The current transaction is rolled
 *                         back and VDBE stops immediately
 *                         with an error.
 *
 *    any        ABORT     Back out changes from the current
 *                         command only (do not do a complete
 *                         rollback) then cause VDBE to return
 *                         immediately with an error.
 *
 *    any        FAIL      VDBE returns immediately with an error.
 *                         The transaction is not rolled back and
 *                         any changes to prior rows are retained.
 *
 *    any       IGNORE     The attempt in insert or update the
 *                         current row is skipped, without
 *                         throwing an error. Processing
 *                         continues with the next row.
 *
 *  NOT NULL    REPLACE    The NULL value is replace by the
 *                         default value for that column. If the
 *                         default value is NULL, the action is
 *                         the same as ABORT.
 *
 *  UNIQUE      REPLACE    The other row that conflicts with the
 *                         row being inserted is removed.
 *                         Triggers are fired, foreign keys
 *                         constraints are checked.
 *
 *  CHECK       REPLACE    Illegal. Results in an exception.
 *
 * @param parse_context Current parsing context.
 * @param space The space being inserted or updated.
 * @param new_tuple_reg First register in a range holding values
 *                      to insert.
 * @param on_conflict On conflict error action of INSERT or
 *        UPDATE statement (for example INSERT OR REPLACE).
 * @param ignore_label Jump to this label on an IGNORE resolution.
 * @param upd_cols Columns to be updated with the size of table's
 *                 field count. NULL for INSERT operation.
 */
void
vdbe_emit_constraint_checks(struct Parse *parse_context,
			    struct space *space, int new_tuple_reg,
			    enum on_conflict_action on_conflict,
			    int ignore_label, int *upd_cols);

/**
 * Gnerate code to make check constraints tests on tuple insertion
 * on INSERT, REPLACE or UPDATE operations.
 * @param parser Current parsing context.
 * @param expr Check constraint AST.
 * @param name Check constraint name to raise an informative
 *             error.
 * @param expr_str Ck constraint expression source string to
 *                 raise an informative error.
 * @param vdbe_field_ref_reg The VDBE register with prepared
 *                           vdbe_field_ref_reg pointer inside is
 *                           initialized with a tuple to be
 *                           inserted.
 */
void
vdbe_emit_ck_constraint(struct Parse *parser, struct Expr *expr,
			const char *name, const char *expr_str,
			int vdbe_field_ref_reg);
/**
 * This routine generates code to finish the INSERT or UPDATE
 * operation that was started by a prior call to
 * vdbe_emit_constraint_checks. It encodes raw data which is held
 * in a range of registers starting from @raw_data_reg and length
 * of @tuple_len and inserts this record to space using given
 * @cursor_id.
 *
 * @param v Virtual database engine.
 * @param space Pointer to space object.
 * @param raw_data_reg Register with raw data to insert.
 * @param tuple_len Number of registers to hold the tuple.
 * @param on_conflict On conflict action.
 * @param autoinc_reg if not 0, then this is the register that
 *                    contains the value that will be inserted
 *                    into the field with AUTOINCREMENT.
 */
void
vdbe_emit_insertion_completion(struct Vdbe *v, struct space *space,
			       int raw_data_reg, uint32_t tuple_len,
			       enum on_conflict_action on_conflict,
			       int autoinc_reg);

void
sql_set_multi_write(Parse *, bool);

Expr *sqlExprDup(sql *, Expr *, int);
SrcList *sqlSrcListDup(sql *, SrcList *, int);
IdList *sqlIdListDup(sql *, IdList *);
Select *sqlSelectDup(sql *, Select *, int);
#ifdef SQL_DEBUG
void sqlSelectSetName(Select *, const char *);
#else
#define sqlSelectSetName(A,B)
#endif

/**
 * Evaluate a view and store its result in an ephemeral table.
 * The where argument is an optional WHERE clause that restricts
 * the set of rows in the view that are to be added to the
 * ephemeral table.
 *
 * @param parse Parsing context.
 * @param name View name.
 * @param where Option WHERE clause to be added.
 * @param cursor Cursor number for ephemeral table.
 */
void
sql_materialize_view(struct Parse *parse, const char *name, struct Expr *where,
		     int cursor);

/**
 * This is called by the parser when it sees a CREATE TRIGGER
 * statement up to the point of the BEGIN before the trigger
 * actions.  A sql_trigger structure is generated based on the
 * information available and stored in parse->parsed_ast.trigger.
 * After the trigger actions have been parsed, the
 * sql_trigger_finish() function is called to complete the trigger
 * construction process.
 */
void
sql_trigger_begin(struct Parse *parse);

/**
 * This routine is called after all of the trigger actions have
 * been parsed in order to complete the process of building the
 * trigger.
 *
 * @param parse Parser context.
 * @param step_list The triggered program.
 * @param token Token that describes the complete CREATE TRIGGER.
 */
void
sql_trigger_finish(struct Parse *parse, struct TriggerStep *step_list,
		   struct Token *token);

/**
 * This function is called from parser to generate drop trigger
 * VDBE code.
 *
 * @param parser Parser context.
 */
void
sql_drop_trigger(struct Parse *parser);

/**
 * Drop a trigger given a pointer to that trigger.
 *
 * @param parser Parser context.
 * @param trigger_name The name of trigger to drop.
 * @param account_changes Increase number of db changes made since
 *        last reset.
 */
void
vdbe_code_drop_trigger(struct Parse *parser, const char *trigger_name,
		       bool account_changes);

/**
 * Return a list of all triggers on space (represented with
 * space_def) if there exists at least one trigger that must be
 * fired when an operation of type 'op' is performed on the
 * table, and, if that operation is an UPDATE, if at least one
 * of the columns in changes_list is being modified.
 *
 * @param space_def The definition of the space that contains
 *        the triggers.
 * @param op operation one of TK_DELETE, TK_INSERT, TK_UPDATE.
 * @param changes_list Columns that change in an UPDATE statement.
 * @param sql_flags SQL flags which describe how to parse request.
 * @param[out] pMask Mask of TRIGGER_BEFORE|TRIGGER_AFTER
 */
struct sql_trigger *
sql_triggers_exist(struct space_def *space_def, int op,
		   struct ExprList *changes_list, uint32_t sql_flags,
		   int *mask_ptr);

/**
 * This is called to code the required FOR EACH ROW triggers for
 * an operation on table. The operation to code triggers for
 * (INSERT, UPDATE or DELETE) is given by the op parameter. The
 * tr_tm parameter determines whether the BEFORE or AFTER triggers
 * are coded. If the operation is an UPDATE, then parameter
 * changes_list is passed the list of columns being modified.
 *
 * If there are no triggers that fire at the specified time for
 * the specified operation on table, this function is a no-op.
 *
 * The reg argument is the address of the first in an array of
 * registers that contain the values substituted for the new.*
 * and old.* references in the trigger program. If N is the number
 * of columns in table table, then registers are populated as
 * follows:
 *
 *   Register       Contains
 *   ------------------------------------------------------
 *   reg+0          OLD.PK
 *   reg+1          OLD.* value of left-most column of space
 *   ...            ...
 *   reg+N          OLD.* value of right-most column of space
 *   reg+N+1        NEW.PK
 *   reg+N+2        OLD.* value of left-most column of space
 *   ...            ...
 *   reg+N+N+1      NEW.* value of right-most column of space
 *
 * For ON DELETE triggers, the registers containing the NEW.*
 * values will never be accessed by the trigger program, so they
 * are not allocated or populated by the caller (there is no data
 * to populate them with anyway). Similarly, for ON INSERT
 * triggers the values stored in the OLD.* registers are never
 * accessed, and so are not allocated by the caller. So, for an
 * ON INSERT trigger, the value passed to this function as
 * parameter reg is not a readable register, although registers
 * (reg+N) through (reg+N+N+1) are.
 *
 * Parameter orconf is the default conflict resolution algorithm
 * for the trigger program to use (REPLACE, IGNORE etc.).
 * Parameter ignoreJump is the instruction that control should
 * jump to if a trigger program raises an IGNORE exception.
 *
 * @param parser Parse context.
 * @param trigger List of triggers on table.
 * @param op operation, one of TK_UPDATE, TK_INSERT, TK_DELETE.
 * @param changes_list Changes list for any UPDATE OF triggers.
 * @param tr_tm One of TRIGGER_BEFORE, TRIGGER_AFTER.
 * @param space The space to code triggers from.
 * @param reg The first in an array of registers.
 * @param orconf ON CONFLICT policy.
 * @param ignore_jump Instruction to jump to for RAISE(IGNORE).
 */
void
vdbe_code_row_trigger(struct Parse *parser, struct sql_trigger *trigger,
		      int op, struct ExprList *changes_list, int tr_tm,
		      struct space *space, int reg, int orconf, int ignore_jump);

/**
 * Generate code for the trigger program associated with trigger
 * p on table table. The reg, orconf and ignoreJump parameters
 * passed to this function are the same as those described in the
 * header function for sql_code_row_trigger().
 *
 * @param parser Parse context.
 * @param trigger Trigger to code.
 * @param space The space to code triggers from.
 * @param reg Reg array containing OLD.* and NEW.* values.
 * @param orconf ON CONFLICT policy.
 * @param ignore_jump Instruction to jump to for RAISE(IGNORE).
 */
void
vdbe_code_row_trigger_direct(struct Parse *parser, struct sql_trigger *trigger,
			     struct space *space, int reg, int orconf,
			     int ignore_jump);

void sqlDeleteTriggerStep(sql *, TriggerStep *);

/**
 * Turn a SELECT statement (that the select parameter points to)
 * into a trigger step.
 * The parser calls this routine when it finds a SELECT statement
 * in body of a TRIGGER.
 *
 * @param db The database connection.
 * @param select The SELECT statement to process. Deleted on
 *        error.
 * @retval Not NULL TriggerStep object on success.
 * @retval NULL Error. The diag message is set.
 */
struct TriggerStep *
sql_trigger_select_step(struct sql *db, struct Select *select);

/**
 * Build a trigger step out of an INSERT statement.
 * The parser calls this routine when it sees an INSERT inside the
 * body of a trigger.
 *
 * @param db The database connection.
 * @param table_name Name of the table into which we insert.
 * @param column_list List of columns in table to insert into. Is
 *        deleted on error.
 * @param select The SELECT statement that supplies values. Is
 *        deleted anyway.
 * @param orconf A conflict processing algorithm.
 * @retval Not NULL TriggerStep object on success.
 * @retval NULL Error. The diag message is set.
 */
struct TriggerStep *
sql_trigger_insert_step(struct sql *db, struct Token *table_name,
			struct IdList *column_list, struct Select *select,
			enum on_conflict_action orconf);

/**
 * Construct a trigger step that implements an UPDATE statemen.
 * The parser calls this routine when it sees an UPDATE statement
 * inside the body of a CREATE TRIGGER.
 *
 * @param db The database connection.
 * @param table_name Name of the table to be updated.
 * @param new_list The SET clause: list of column and new values.
 *        Is deleted anyway.
 * @param where The WHERE clause. Is deleted anyway.
 * @param orconf A conflict processing algorithm.
 * @retval Not NULL TriggerStep object on success.
 * @retval NULL Error. The diag message is set.
 */
struct TriggerStep *
sql_trigger_update_step(struct sql *db, struct Token *table_name,
		        struct ExprList *new_list, struct Expr *where,
			enum on_conflict_action orconf);

/**
 * Construct a trigger step that implements a DELETE statement.
 * The parser calls this routine when it sees a DELETE statement
 * inside the body of a CREATE TRIGGER.
 *
 * @param db The database connection.
 * @param table_name The table from which rows are deleted.
 * @param where The WHERE clause. Is deleted anyway.
 * @retval Not NULL TriggerStep object on success.
 * @retval NULL Error. The diag message is set.
 */
struct TriggerStep *
sql_trigger_delete_step(struct sql *db, struct Token *table_name,
			struct Expr *where);

/**
 * Triggers may access values stored in the old.* or new.*
 * pseudo-table.
 * This function returns a 32-bit bitmask indicating which columns
 * of the old.* or new.* tables actually are used by triggers.
 * This information may be used by the caller, for example, to
 * avoid having to load the entire old.* record into memory when
 * executing an UPDATE or DELETE command.
 *
 * Bit 0 of the returned mask is set if the left-most column of
 * the table may be accessed using an [old|new].<col> reference.
 * Bit 1 is set if the second leftmost column value is required,
 * and so on. If there are more than 32 columns in the table, and
 * at least one of the columns with an index greater than 32 may
 * be accessed, 0xffffffff is returned.
 *
 * It is not possible to determine if the old.PK or new.PK column
 * is accessed by triggers. The caller must always assume that it
 * is.
 *
 * Parameter isNew must be either 1 or 0. If it is 0, then the
 * mask returned applies to the old.* table. If 1, the new.* table.
 *
 * Parameter tr_tm must be a mask with one or both of the
 * TRIGGER_BEFORE and TRIGGER_AFTER bits set. Values accessed by
 * BEFORE triggers are only included in the returned mask if the
 * TRIGGER_BEFORE bit is set in the tr_tm parameter. Similarly,
 * values accessed by AFTER triggers are only included in the
 * returned mask if the TRIGGER_AFTER bit is set in tr_tm.
 *
 * @param parser  Parse context.
 * @param trigger List of triggers on table.
 * @param changes_list Changes list for any UPDATE OF triggers.
 * @param new  1 for new.* ref mask, 0 for old.* ref mask.
 * @param tr_tm Mask of TRIGGER_BEFORE|TRIGGER_AFTER.
 * @param space The space to code triggers from.
 * @param orconf Default ON CONFLICT policy for trigger steps.
 *
 * @retval mask value.
 */
uint64_t
sql_trigger_colmask(Parse *parser, struct sql_trigger *trigger,
		    ExprList *changes_list, int new, int tr_tm,
		    struct space *space, int orconf);
#define sqlParseToplevel(p) ((p)->pToplevel ? (p)->pToplevel : (p))
#define sqlIsToplevel(p) ((p)->pToplevel==0)

int sqlJoinType(Parse *, Token *, Token *, Token *);

/**
 * Change defer mode of last FK constraint processed during
 * <CREATE TABLE> statement.
 *
 * @param parse_context Current parsing context.
 * @param is_deferred Change defer mode to this value.
 */
void
fk_constraint_change_defer_mode(struct Parse *parse_context, bool is_deferred);

/**
 * Function called from parser to handle
 * <ALTER TABLE child ADD CONSTRAINT constraint
 *     FOREIGN KEY (child_cols) REFERENCES parent (parent_cols)>
 * OR to handle <CREATE TABLE ...>
 *
 * @param parse_context Parsing context.
 */
void
sql_create_foreign_key(struct Parse *parse_context);

/**
 * Emit code to drop the entry from _index or _ck_contstraint or
 * _fk_constraint space corresponding with the constraint type.
 *
 * Function called from parser to handle
 * <ALTER TABLE table DROP CONSTRAINT constraint> SQL statement.
 *
 * @param parse_context Parsing context.
 */
void
sql_drop_constraint(struct Parse *parse_context);

/**
 * Now our SQL implementation can't operate on spaces which
 * lack format: it is reasonable since for instance we can't
 * resolve column names, their types etc. In case of format
 * absence, diag error is raised.
 *
 * @retval 0 in case space features format.
 * @retval -1 if space doesn't have format.
 */
int
sql_space_def_check_format(const struct space_def *space_def);

/**
 * Counts the trail bytes for a UTF-8 lead byte of a valid UTF-8
 * sequence.
 *
 * Note that implementation is borrowed from ICU library.
 * It is not directly included from icu/utf8.h owing to the
 * fact that different versions of ICU treat incorrect byte
 * sequences in different ways. We like this implementation
 * but don't like that it could give different results depending
 * on version of library. And that's why we inlined these macros.
 *
 * @param lead_byte The first byte of a UTF-8 sequence.
 */
#define SQL_UTF8_COUNT_TRAIL_BYTES(lead_byte) \
	(((uint8_t)(lead_byte) >= 0xc2) + ((uint8_t)(lead_byte) >= 0xe0) + \
	((uint8_t)(lead_byte) >= 0xf0))

/**
 * Advance the string offset from one code point boundary to the
 * next. (Post-incrementing iteration.)
 *
 * After the whole string is traversed, (str + i) points to the
 * position right after the last element of the string (*).
 *
 * If resulting offset > byte_size then resulting offset is set
 * to byte_size. This is to provide (*) in cases where it might
 * be violated.
 *
 * SQL_UTF8_FWD_1 sometimes is used to get the size of utf-8
 * character sub-sequence and we don't want to get summary size
 * which exceeds total string size (in bytes). Consider example:
 *
 * '0xE0' - this is invalid utf-8 string because it consists only
 * of first byte of 3 byte sequence. After traverse, the
 * offset == 2 and we set it to 1, to keep (*).
 *
 * @param s const uint8_t * string.
 * @param i string offset.
 * @param byte_size byte size of the string.
 */
#define SQL_UTF8_FWD_1(str, i, byte_size) \
	(i) += 1 + SQL_UTF8_COUNT_TRAIL_BYTES((str)[i]); \
	(i) = (i) <= (byte_size) ? (i) : (byte_size);

void sqlDetach(Parse *, Expr *);
int sqlAtoF(const char *z, double *, int);
int sqlGetInt32(const char *, int *);

/**
 * Return number of symbols in the given string.
 *
 * Number of symbols != byte size of string because some symbols
 * are encoded with more than one byte. Also note that all
 * symbols from 'str' to 'str + byte_len' would be counted,
 * even if there is a '\0' somewhere between them.
 *
 * This function is implemented to be fast and indifferent to
 * correctness of string being processed. If input string has
 * even one invalid utf-8 sequence, then the resulting length
 * could be arbitary in these boundaries (0 < len < byte_len).
 * @param str String to be counted.
 * @param byte_len Byte length of given string.
 * @return number of symbols in the given string.
 */
int
sql_utf8_char_count(const unsigned char *str, int byte_len);

u32 sqlUtf8Read(const u8 **);
LogEst sqlLogEst(u64);
LogEst sqlLogEstAdd(LogEst, LogEst);
u64 sqlLogEstToInt(LogEst);
VList *sqlVListAdd(sql *, VList *, const char *, int, int);
const char *sqlVListNumToName(VList *, int);
int sqlVListNameToNum(VList *, const char *, int);

/*
 * Routines to read and write variable-length integers.  These used to
 * be defined locally, but now we use the varint routines in the util.c
 * file.
 */
int sqlPutVarint(unsigned char *, u64);
u8 sqlGetVarint(const unsigned char *, u64 *);
u8 sqlGetVarint32(const unsigned char *, u32 *);
int sqlVarintLen(u64 v);

/*
 * The common case is for a varint to be a single byte.  They following
 * macros handle the common case without a procedure call, but then call
 * the procedure for larger varints.
 */
#define getVarint32(A,B)  \
  (u8)((*(A)<(u8)0x80)?((B)=(u32)*(A)),1:sqlGetVarint32((A),(u32 *)&(B)))
#define putVarint32(A,B)  \
  (u8)(((u32)(B)<(u32)0x80)?(*(A)=(unsigned char)(B)),1:\
  sqlPutVarint((A),(B)))
#define getVarint    sqlGetVarint
#define putVarint    sqlPutVarint

/** Return string consisting of fields types of given index. */
enum field_type *
sql_index_type_str(struct sql *db, const struct index_def *idx_def);

/**
 * Code an OP_ApplyType opcode that will force types
 * for given range of register starting from @reg.
 *
 * @param v VDBE.
 * @param def Definition of table to be used.
 * @param reg Register where types will be placed.
 */
void
sql_emit_table_types(struct Vdbe *v, struct space_def *def, int reg);

enum field_type
sql_type_result(enum field_type lhs, enum field_type rhs);

/**
 * pExpr is a comparison operator. Return the type affinity
 * that should be applied to both operands prior to doing
 * the comparison.
 */
enum field_type
expr_cmp_mutual_type(struct Expr *pExpr);

/**
 * Return the type of the expression pExpr.
 *
 * If pExpr is a column, a reference to a column via an 'AS' alias,
 * or a sub-select with a column as the return value, then the
 * type of that column is returned. Otherwise, type ANY is returned,
 * indicating that the expression can feature any type.
 *
 * The WHERE clause expressions in the following statements all
 * have an type:
 *
 * CREATE TABLE t1(a);
 * SELECT * FROM t1 WHERE a;
 * SELECT a AS b FROM t1 WHERE b;
 * SELECT * FROM t1 WHERE (select a from t1);
 */
enum field_type
sql_expr_type(struct Expr *pExpr);

/**
 * This function duplicates first @len entries of types array
 * and terminates new array with field_type_MAX member.
 */
enum field_type *
field_type_sequence_dup(struct Parse *parse, enum field_type *types,
			uint32_t len);

/**
 * Convert z to a 64-bit signed or unsigned integer.
 * z must be decimal. This routine does *not* accept
 * hexadecimal notation. Under the hood it calls
 * strtoll or strtoull depending on presence of '-' char.
 *
 * length is the number of bytes in the string (bytes, not
 * characters). The string is not necessarily zero-terminated.
 *
 * @param z String being parsed.
 * @param[out] val Output integer value.
 * @param[out] is_neg Sign of the result.
 * @param length String length in bytes.
 * @retval 0 Successful transformation. Fits in a 64-bit signed
 *           or unsigned integer.
 * @retval -1 Error during parsing: it contains non-digit
 *            characters or it doesn't fit into 64-bit int.
 */
int
sql_atoi64(const char *z, int64_t *val, bool *is_neg, int length);

void *sqlHexToBlob(sql *, const char *z, int n);
u8 sqlHexToInt(int h);

/**
 * Return the collation sequence for the expression pExpr. If
 * there is no defined collating sequence, return NULL.
 *
 * The collating sequence might be determined by a COLLATE operator
 * or by the presence of a column with a defined collating sequence.
 * COLLATE operators take first precedence.  Left operands take
 * precedence over right operands.
 *
 * @param parse Parsing context.
 * @param expr Expression to scan.
 * @param[out] is_explicit_coll Flag set if explicit COLLATE
 *             clause is used.
 * @param[out] coll_id Collation identifier.
 * @param[out] coll Collation object.
 *
 * @retval 0 on success.
 * @retval -1 in case of error.
 */
int
sql_expr_coll(Parse *parse, Expr *p, bool *is_explicit_coll, uint32_t *coll_id,
	      struct coll **coll);

Expr *sqlExprAddCollateToken(Parse * pParse, Expr *, const Token *, int);
Expr *sqlExprAddCollateString(Parse *, Expr *, const char *);
Expr *sqlExprSkipCollate(Expr *);
int sqlCheckIdentifierName(Parse *, char *);
void sqlVdbeSetChanges(sql *, int);

/**
 * Attempt to add, subtract, multiply or get the remainder of
 * 64-bit integer values. There functions are able to operate
 * on signed as well as unsigned integers. If result of operation
 * is greater 0, then it is assumed to be unsigned and can take
 * values in range up to 2^64 - 1. If the result is negative,
 * then its minimum value is -2^63.
 * Return 0 on success.  Or if the operation would have resulted
 * in an overflow, return -1.
 */
int
sql_add_int(int64_t lhs, bool is_lhs_neg, int64_t rhs, bool is_rhs_neg,
	    int64_t *res, bool *is_res_neg);

int
sql_sub_int(int64_t lhs, bool is_lhs_neg, int64_t rhs, bool is_rhs_neg,
	    int64_t *res, bool *is_res_neg);

int
sql_mul_int(int64_t lhs, bool is_lhs_neg, int64_t rhs, bool is_rhs_neg,
	    int64_t *res, bool *is_res_neg);

int
sql_div_int(int64_t lhs, bool is_lhs_neg, int64_t rhs, bool is_rhs_neg,
	    int64_t *res, bool *is_res_neg);

int
sql_rem_int(int64_t lhs, bool is_lhs_neg, int64_t rhs, bool is_rhs_neg,
	    int64_t *res, bool *is_res_neg);

int sqlValueFromExpr(sql *, Expr *, enum field_type type,
			 sql_value **);

extern const unsigned char sqlOpcodeProperty[];
extern const unsigned char sqlUpperToLower[];
extern const unsigned char sqlCtypeMap[];
extern const Token sqlIntTokens[];
extern SQL_WSD struct sqlConfig sqlConfig;
extern int sqlPendingByte;

/**
 * Generate code to implement the "ALTER TABLE xxx RENAME TO yyy"
 * command.
 *
 * @param parse Current parsing context.
 */
void
sql_alter_table_rename(struct Parse *parse);

/**
 * Generate code to implement the "ALTER TABLE xxx ENABLE/DISABLE
 * CHECK CONSTRAINT" command.
 *
 * @param parse Current parsing context.
 */
void
sql_alter_ck_constraint_enable(struct Parse *parse);

/**
 * Return the length (in bytes) of the token that begins at z[0].
 * Store the token type in *type before returning.
 *
 * @param z Input stream.
 * @param[out] type Detected type of token.
 * @param[out] is_reserved True if reserved word.
 */
int
sql_token(const char *z, int *type, bool *is_reserved);

void sqlExpirePreparedStatements(sql *);
int sqlCodeSubselect(Parse *, Expr *, int);
void sqlSelectPrep(Parse *, Select *, NameContext *);

/**
 * Returns name of the connection operator.
 *
 * @param id ID of the connection operator.
 * @retval Name of the connection operator.
 */
const char *
sql_select_op_name(int id);

int sqlMatchSpanName(const char *, const char *, const char *);
int sqlResolveExprNames(NameContext *, Expr *);
int sqlResolveExprListNames(NameContext *, ExprList *);
void sqlResolveSelectNames(Parse *, Select *, NameContext *);
int sqlResolveOrderGroupBy(Parse *, Select *, ExprList *, const char *);

char* rename_trigger(sql *, char const *, char const *, bool *);
/**
 * Find a collation by name. Set error in @a parser if not found.
 * @param parser Parser.
 * @param name Collation name.
 * @param[out] Collation identifier.
 *
 * @retval Collation object. NULL on error or not found.
 */
struct coll *
sql_get_coll_seq(Parse *parser, const char *name, uint32_t *coll_id);

/**
 * This function returns average size of tuple in given index.
 * Currently, all indexes from one space feature the same size,
 * due to the absence of partial indexes.
 *
 * @param space Index belongs to this space.
 * @param idx Index to be examined.
 * @retval Average size of tuple in given index.
 */
ssize_t
sql_index_tuple_size(struct space *space, struct index *idx);

/**
 * Load the content of the _sql_stat1 and sql_stat4 tables. The
 * contents of _sql_stat1 are used to populate the tuple_stat1[]
 * arrays. The contents of sql_stat4 are used to populate the
 * samples[] arrays.
 *
 * @param db Database handler.
 * @retval 0 Success.
 * @retval -1 Error.
 */
int
sql_analysis_load(struct sql *db);

/**
 * An instance of the following structure controls how keys
 * are compared by VDBE, see P4_KEYINFO.
 */
struct sql_key_info {
	sql *db;
	/**
	 * Key definition created from this object,
	 * see sql_key_info_to_key_def().
	 */
	struct key_def *key_def;
	/** Reference counter. */
	uint32_t refs;
	/** Rowid should be the only part of PK, if true. */
	bool is_pk_rowid;
	/** Number of parts in the key. */
	uint32_t part_count;
	/** Definition of the key parts. */
	struct key_part_def parts[];
};

/**
 * Allocate a key_info object sufficient for an index with
 * the given number of key columns.
 */
struct sql_key_info *
sql_key_info_new(sql *db, uint32_t part_count);

/**
 * Allocate a key_info object from the given key definition.
 */
struct sql_key_info *
sql_key_info_new_from_key_def(sql *db, const struct key_def *key_def);

/**
 * Increment the reference counter of a key_info object.
 */
struct sql_key_info *
sql_key_info_ref(struct sql_key_info *key_info);

/**
 * Decrement the reference counter of a key_info object and
 * free memory if the object isn't referenced anymore.
 */
void
sql_key_info_unref(struct sql_key_info *key_info);

/**
 * Create a key definition from a key_info object.
 * The new key definition is cached in key_info struct
 * so that subsequent calls to this function are free.
 */
struct key_def *
sql_key_info_to_key_def(struct sql_key_info *key_info);

/**
 * Check if the function implements LIKE-style comparison & if it
 * is appropriate to apply a LIKE query optimization.
 *
 * @param pExpr pointer to a function-implementing expression.
 * @param[out] is_like_ci true if LIKE is case insensitive.
 *
 * @retval 1 if LIKE optimization can be used, 0 otherwise.
 */
int
sql_is_like_func(struct Expr *expr);

/** Set OOM error flag. */
static inline void
sqlOomFault(struct sql *db)
{
	db->mallocFailed = 1;
	db->lookaside.bDisable++;
}

void sqlOomClear(sql *);

void sqlStrAccumInit(StrAccum *, sql *, char *, int, int);
void sqlStrAccumAppend(StrAccum *, const char *, int);
void sqlStrAccumAppendAll(StrAccum *, const char *);
void sqlAppendChar(StrAccum *, int, char);
char *sqlStrAccumFinish(StrAccum *);
void sqlStrAccumReset(StrAccum *);
void sqlSelectDestInit(SelectDest *, int, int, int);

/*
 * Create an expression to load @a column from datasource
 * @a src_idx in @a src_list.
 *
 * @param db The database connection.
 * @param src_list The source list described with FROM clause.
 * @param src_idx The resource index to use in src_list.
 * @param column The column index.
 * @retval Not NULL Success. An expression to load @a column.
 * @retval NULL Error. A diag message is set.
 */
struct Expr *
sql_expr_new_column(struct sql *db, struct SrcList *src_list, int src_idx,
		    int column);

int sqlExprCheckIN(Parse *, Expr *);

/* TODO: Enable this function when stat-tables will be revived. */
static inline int
sqlStat4ProbeSetValue(struct Parse *parse, ...)
{
	(void)parse;
	unreachable();
	return 0;
}

/* TODO: Enable this function when stat-tables will be revived. */
static inline int
sqlStat4ValueFromExpr(struct Parse *parse, ...)
{
	(void)parse;
	unreachable();
	return 0;
}

/* TODO: Enable this function when stat-tables will be revived. */
static inline void
sqlStat4ProbeFree(struct UnpackedRecord *rec)
{
	(void)rec;
}

/* TODO: Enable this function when stat-tables will be revived. */
static inline int
sql_stat4_column(struct sql *db, ...)
{
	(void)db;
	return 0;
}

/*
 * The interface to the LEMON-generated parser
 */
void *sqlParserAlloc(void *(*)(u64));
void sqlParserFree(void *, void (*)(void *));
void sqlParser(void *, int, Token, Parse *);
#ifdef YYTRACKMAXSTACKDEPTH
int sqlParserStackPeak(void *);
#endif

sql_int64 sqlStmtCurrentTime(sql_context *);
int sqlVdbeParameterIndex(Vdbe *, const char *, int);
int sqlTransferBindings(sql_stmt *, sql_stmt *);
int sqlReprepare(Vdbe *);

/**
 * This function verifies that two collations (to be more precise
 * their ids) are compatible. In terms of SQL ANSI they are
 * compatible if:
 *  - one of collations is mentioned alongside with explicit
 *    COLLATE clause, which forces this collation over another
 *    one. It is allowed to have the same forced collations;
 * - both collations are derived from table columns and they
 *   are the same;
 * - one collation is derived from table column and another
 *   one is not specified (i.e. COLL_NONE);
 * In all other cases they are not accounted to be compatible
 * and error should be raised.
 * Collation to be used in comparison operator is returned
 * via @res_id: in case one of collations is absent, then
 * the second one is utilized.
 */
int
collations_check_compatibility(uint32_t lhs_id, bool is_lhs_forced,
			       uint32_t rhs_id, bool is_rhs_forced,
			       uint32_t *res_id);

/**
 * Return a pointer to the collation sequence that should be used
 * by a binary comparison operator comparing left and right.
 *
 * If the left hand expression has a collating sequence type, then
 * it is used. Otherwise the collation sequence for the right hand
 * expression is used, or the default (BINARY) if neither
 * expression has a collating type.
 *
 * Argument right (but not left) may be a null pointer. In this
 * case, it is not considered.
 * @param parser Parser.
 * @param left Left expression.
 * @param right Right expression. Can be NULL.
 * @param[out] id Id of resulting collation.
 *
 * @retval 0 on success.
 * @retval -1 in case of error (e.g. no collation found).
 */
int
sql_binary_compare_coll_seq(Parse *parser, Expr *left, Expr *right,
			    uint32_t *id);
With *sqlWithAdd(Parse *, With *, Token *, ExprList *, Select *);
void sqlWithDelete(sql *, With *);
void sqlWithPush(Parse *, With *, u8);

/*
 * This function is called when inserting, deleting or updating a
 * row of table tab to generate VDBE code to perform foreign key
 * constraint processing for the operation.
 *
 * For a DELETE operation, parameter reg_old is passed the index
 * of the first register in an array of (tab->def->field_count +
 * 1) registers containing the PK of the row being deleted,
 * followed by each of the column values of the row being deleted,
 * from left to right. Parameter reg_new is passed zero in this
 * case.
 *
 * For an INSERT operation, reg_old is passed zero and reg_new is
 * passed the first register of an array of
 * (tab->def->field_count + 1) registers containing the new row
 * data.
 *
 * For an UPDATE operation, this function is called twice. Once
 * before the original record is deleted from the table using the
 * calling convention described for DELETE. Then again after the
 * original record is deleted but before the new record is
 * inserted using the INSERT convention.
 *
 * @param parser SQL parser.
 * @param space Space from which the row is deleted.
 * @param reg_old Register with deleted row.
 * @param reg_new Register with inserted row.
 * @param changed_cols Array of updated columns. Can be NULL.
 */
void
fk_constraint_emit_check(struct Parse *parser, struct space *space, int reg_old,
		int reg_new, const int *changed_cols);

/**
 * Emit VDBE code to do CASCADE, SET NULL or SET DEFAULT actions
 * when deleting or updating a row.
 * @param parser SQL parser.
 * @param space Space being updated or deleted from.
 * @param reg_old Register of the old record.
 * param changes Array of numbers of changed columns.
 */
void
fk_constraint_emit_actions(struct Parse *parser, struct space *space, int reg_old,
		  const int *changes);

/**
 * This function is called before generating code to update or
 * delete a row contained in given space. If the operation is
 * a DELETE, then parameter changes is passed a NULL value.
 * For an UPDATE, changes points to an array of size N, where N
 * is the number of columns in table. If the i'th column is not
 * modified by the UPDATE, then the corresponding entry in the
 * changes[] array is set to -1. If the column is modified,
 * the value is 0 or greater.
 *
 * @param space Space to be modified.
 * @param changes Array of modified fields for UPDATE.
 * @retval True, if any foreign key processing will be required.
 */
bool
fk_constraint_is_required(struct space *space, const int *changes);

/*
 * Allowed return values from sqlFindInIndex()
 */
#define IN_INDEX_EPH          2	/* Search an ephemeral b-tree */
#define IN_INDEX_INDEX_ASC    3	/* Existing index ASCENDING */
#define IN_INDEX_INDEX_DESC   4	/* Existing index DESCENDING */
#define IN_INDEX_NOOP         5	/* No table available. Use comparisons */
/*
 * Allowed flags for the 3rd parameter to sqlFindInIndex().
 */
#define IN_INDEX_NOOP_OK     0x0001	/* OK to return IN_INDEX_NOOP */
#define IN_INDEX_MEMBERSHIP  0x0002	/* IN operator used for membership test */
#define IN_INDEX_LOOP        0x0004	/* IN operator used as a loop */
int sqlFindInIndex(Parse *, Expr *, u32, int *, int *, int *);

void sqlExprSetHeightAndFlags(Parse * pParse, Expr * p);
#if SQL_MAX_EXPR_DEPTH>0
int sqlSelectExprHeight(Select *);
int sqlExprCheckHeight(Parse *, int);
#else
#define sqlSelectExprHeight(x) 0
#define sqlExprCheckHeight(x,y)
#endif

#ifdef SQL_DEBUG
void sqlParserTrace(FILE *, char *);
#endif

int sqlExprVectorSize(Expr * pExpr);
int sqlExprIsVector(Expr * pExpr);
Expr *sqlVectorFieldSubexpr(Expr *, int);
Expr *sqlExprForVectorField(Parse *, Expr *, int);

/* Tarantool: right now query compilation is invoked on top of
 * fiber's stack. Need to limit number of nested programs under
 * compilation to avoid stack overflow.
 */
extern int sqlSubProgramsRemaining;

struct func_sql_builtin {
	/** Function object base class. */
	struct func base;
	/** A bitmask of SQL flags. */
	uint16_t flags;
	/**
	 * A VDBE-memory-compatible call method.
	 * SQL built-ins don't use func base class "call"
	 * method to provide a best performance for SQL requests.
	 * Access checks are redundant, because all SQL built-ins
	 * are predefined and are executed on SQL privilege level.
	 */
	void (*call)(sql_context *ctx, int argc, sql_value **argv);
	/**
	 * A VDBE-memory-compatible finalize method
	 * (is valid only for aggregate function).
	 */
	void (*finalize)(sql_context *ctx);
};

/**
 * Test whether SQL-specific flag is set for given function.
 * Currently only SQL Builtin Functions have such hint flags,
 * so function returns false for other functions. Such approach
 * decreases code complexity and allows do not distinguish
 * functions by implementation details where it is unnecessary.
 *
 * Returns true when given flag is set for a given function and
 * false otherwise.
 */
static inline bool
sql_func_flag_is_set(struct func *func, uint16_t flag)
{
	if (func->def->language != FUNC_LANGUAGE_SQL_BUILTIN)
		return false;
	return (((struct func_sql_builtin *)func)->flags & flag) != 0;
}

/**
 * A SQL method to find a function in a hash by its name and
 * count of arguments. Only functions that have 'SQL' engine
 * export field set true and have exactly the same signature
 * are returned.
 *
 * Returns not NULL function pointer when a valid and exported
 * to SQL engine function is found and NULL otherwise.
 */
struct func *
sql_func_by_signature(const char *name, int argc);

/**
 * Generate VDBE code to halt execution with correct error if
 * the object with specified key is already present (or doesn't
 * present - configure with cond_opcodeq) in specified space.
 * The function allocates error and name resources for VDBE
 * itself.
 *
 * @param parser Parsing context.
 * @param space_id Space to lookup identifier.
 * @param index_id Index identifier of key.
 * @param key_reg Register where key to be found is held.
 * @param key_len Lenght of key (number of resiters).
 * @param tarantool_error_code to set on halt.
 * @param error_src Error message to display on VDBE halt.
 * @param no_error Do not raise error flag.
 * @param cond_opcode Condition to raise - OP_NoConflict or
 *        OP_Found.
 *
 * @retval -1 on memory allocation error.
 * @retval 0 on success.
 */
int
vdbe_emit_halt_with_presence_test(struct Parse *parser, int space_id,
				  int index_id, int key_reg, uint32_t key_len,
				  int tarantool_error_code,
				  const char *error_src, bool no_error,
				  int cond_opcode);

/**
 * Generate VDBE code to delete records from system _sql_stat1 or
 * _sql_stat4 table.
 *
 * @param parse The parsing context.
 * @param stat_table_name System stat table name.
 * @param idx_name Index name.
 * @param table_name Table name.
 */
void
vdbe_emit_stat_space_clear(struct Parse *parse, const char *stat_table_name,
			   const char *idx_name, const char *table_name);

/**
 * Add AUTOINCREMENT feature for one of INTEGER or UNSIGNED fields
 * of PRIMARY KEY.
 *
 * @param parse_context Parsing context.
 * @param fieldno Field number in space format under construction.
 *
 * @retval 0 on success.
 * @retval -1 if table already has declared AUTOINCREMENT feature.
 */
int
sql_add_autoincrement(struct Parse *parse_context, uint32_t fieldno);

/**
 * Get fieldno by field name. At the moment of forming space format
 * there's no tuple dictionary, so we can't use hash, in contrast to
 * tuple_fieldno_by_name(). However, AUTOINCREMENT can occur at most
 * once in table's definition, so it's not a big deal if we use O(n)
 * search.
 *
 * @param parse_context Parsing context.
 * @param field_name Expr that contains field name.
 * @param fieldno[out] Field number in new space format.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sql_fieldno_by_name(struct Parse *parse_context, struct Expr *field_name,
		    uint32_t *fieldno);

/**
 * Create VDBE instructions to set the new value of the session setting.
 *
 * @param parse_context Parsing context.
 * @param name Name of the session setting.
 * @param value New value of the session setting.
 */
void
sql_setting_set(struct Parse *parse_context, struct Token *name,
		struct Expr *value);

/**
 * Return a string of the form "COLUMN_N", where N is @a number.
 *
 * We decided to name every auto generated column in output by
 * this pattern (like PostgreSQL), because it is more convenient
 * than "_auto_name_" and naming with span like MariaDB do.
 */
static inline const char *
sql_generate_column_name(uint32_t number)
{
	return tt_sprintf("COLUMN_%d", number);
}

#endif				/* sqlINT_H */
