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
 * Internal interface definitions for SQLite.
 *
 */
#ifndef SQLITEINT_H
#define SQLITEINT_H

#define IdChar(C)  ((sqlite3CtypeMap[(unsigned char)C]&0x46)!=0)

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

#include "box/field_def.h"
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
 * Large file support can be disabled using the -DSQLITE_DISABLE_LFS switch
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
 * lack LFS in which case the SQLITE_DISABLE_LFS macro might still be useful.
 *
 * Similar is true for Mac OS X.  LFS is only supported on Mac OS X 9 and later.
 */
#ifndef SQLITE_DISABLE_LFS
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

#include "sqliteLimit.h"

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
#define SQLITE_INT_TO_PTR(X)  ((void*)(__PTRDIFF_TYPE__)(X))
#define SQLITE_PTR_TO_INT(X)  ((int)(__PTRDIFF_TYPE__)(X))
#elif !defined(__GNUC__)	/* Works for compilers other than LLVM */
#define SQLITE_INT_TO_PTR(X)  ((void*)&((char*)0)[X])
#define SQLITE_PTR_TO_INT(X)  ((int)(((char*)X)-(char*)0))
#elif defined(HAVE_STDINT_H)	/* Use this case if we have ANSI headers */
#define SQLITE_INT_TO_PTR(X)  ((void*)(intptr_t)(X))
#define SQLITE_PTR_TO_INT(X)  ((int)(intptr_t)(X))
#else				/* Generates a warning - but it always works */
#define SQLITE_INT_TO_PTR(X)  ((void*)(X))
#define SQLITE_PTR_TO_INT(X)  ((int)(X))
#endif

/*
 * A macro to hint to the compiler that a function should not be
 * inlined.
 */
#if defined(__GNUC__)
#define SQLITE_NOINLINE  __attribute__((noinline))
#else
#define SQLITE_NOINLINE
#endif

/*
 * Powersafe overwrite is on by default.  But can be turned off using
 * the -DSQLITE_POWERSAFE_OVERWRITE=0 command-line option.
 */
#ifndef SQLITE_POWERSAFE_OVERWRITE
#define SQLITE_POWERSAFE_OVERWRITE 1
#endif

/*
 * EVIDENCE-OF: R-25715-37072 Memory allocation statistics are enabled by
 * default unless SQLite is compiled with SQLITE_DEFAULT_MEMSTATUS=0 in
 * which case memory allocation statistics are disabled by default.
 */
#if !defined(SQLITE_DEFAULT_MEMSTATUS)
#define SQLITE_DEFAULT_MEMSTATUS 1
#endif

#if defined(SQLITE_SYSTEM_MALLOC) \
  + defined(SQLITE_ZERO_MALLOC) \
  + defined(SQLITE_MEMDEBUG)>1
#error "Two or more of the following compile-time configuration options\
 are defined but at most one is allowed:\
 SQLITE_SYSTEM_MALLOC, SQLITE_MEMDEBUG,\
 SQLITE_ZERO_MALLOC"
#endif
#if defined(SQLITE_SYSTEM_MALLOC) \
  + defined(SQLITE_ZERO_MALLOC) \
  + defined(SQLITE_MEMDEBUG)==0
#define SQLITE_SYSTEM_MALLOC 1
#endif

/*
 * If SQLITE_MALLOC_SOFT_LIMIT is not zero, then try to keep the
 * sizes of memory allocations below this value where possible.
 */
#if !defined(SQLITE_MALLOC_SOFT_LIMIT)
#define SQLITE_MALLOC_SOFT_LIMIT 1024
#endif

/*
 * Enable SQLITE_ENABLE_EXPLAIN_COMMENTS if SQLITE_DEBUG is turned on.
 */
#if !defined(SQLITE_ENABLE_EXPLAIN_COMMENTS) && defined(SQLITE_DEBUG)
#define SQLITE_ENABLE_EXPLAIN_COMMENTS 1
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
#ifdef SQLITE_COVERAGE_TEST
void sqlite3Coverage(int);
#define testcase(X)  if( X ){ sqlite3Coverage(__LINE__); }
#else
#define testcase(X)
#endif

/*
 * The TESTONLY macro is used to enclose variable declarations or
 * other bits of code that are needed to support the arguments
 * within testcase() and assert() macros.
 */
#if !defined(NDEBUG) || defined(SQLITE_COVERAGE_TEST)
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
 * of SQLite to unexpected behavior - to make the code "self-healing"
 * or "ductile" rather than being "brittle" and crashing at the first
 * hint of unplanned behavior.
 *
 * In other words, ALWAYS and NEVER are added for defensive code.
 *
 * When doing coverage testing ALWAYS and NEVER are hard-coded to
 * be true and false so that the unreachable code they specify will
 * not be counted as untested code.
 */
#if defined(SQLITE_COVERAGE_TEST) || defined(SQLITE_MUTATION_TEST)
#define ALWAYS(X)      (1)
#define NEVER(X)       (0)
#elif !defined(NDEBUG)
#define ALWAYS(X)      ((X)?1:(assert(0),0))
#define NEVER(X)       ((X)?(assert(0),1):0)
#else
#define ALWAYS(X)      (X)
#define NEVER(X)       (X)
#endif

/*
 * Some malloc failures are only possible if SQLITE_TEST_REALLOC_STRESS is
 * defined.  We need to defend against those failures when testing with
 * SQLITE_TEST_REALLOC_STRESS, but we don't want the unreachable branches
 * during a normal build.  The following macro can be used to disable tests
 * that are always false except when SQLITE_TEST_REALLOC_STRESS is set.
 */
#if defined(SQLITE_TEST_REALLOC_STRESS)
#define ONLY_IF_REALLOC_STRESS(X)  (X)
#elif !defined(NDEBUG)
#define ONLY_IF_REALLOC_STRESS(X)  ((X)?(assert(0),1):0)
#else
#define ONLY_IF_REALLOC_STRESS(X)  (0)
#endif

/*
 * Is the sqlite3ErrName() function needed in the build?  Currently,
 * it is needed by several "test*.c" files (which are
 * compiled using SQLITE_TEST).
 */
#if defined(SQLITE_TEST)
#define SQLITE_NEED_ERR_NAME
#else
#undef  SQLITE_NEED_ERR_NAME
#endif

/*
 * Return true (non-zero) if the input is an integer that is too large
 * to fit in 32-bits.  This macro is used inside of various testcase()
 * macros to verify that we have tested SQLite for large-file support.
 */
#define IS_BIG_INT(X)  (((X)&~(i64)0xffffffff)!=0)

#include "hash.h"
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

typedef long long int sqlite_int64;
typedef unsigned long long int sqlite_uint64;
typedef sqlite_int64 sqlite3_int64;
typedef sqlite_uint64 sqlite3_uint64;
typedef struct sqlite3_stmt sqlite3_stmt;

typedef struct sqlite3_context sqlite3_context;
typedef struct sqlite3 sqlite3;
typedef struct Mem sqlite3_value;
typedef struct sqlite3_file sqlite3_file;
struct sqlite3_file {
	const struct sqlite3_io_methods *pMethods;	/* Methods for an open file */
};

typedef int (*sqlite3_callback) (void *, int, char **, char **);

typedef struct sqlite3_vfs sqlite3_vfs;
typedef void (*sqlite3_syscall_ptr) (void);
struct sqlite3_vfs {
	int iVersion;	/* Structure version number (currently 3) */
	int szOsFile;	/* Size of subclassed sqlite3_file */
	int mxPathname;	/* Maximum file pathname length */
	sqlite3_vfs *pNext;	/* Next registered VFS */
	const char *zName;	/* Name of this virtual file system */
	void *pAppData;	/* Pointer to application-specific data */
	int (*xOpen) (sqlite3_vfs *, const char *zName, sqlite3_file *,
		      int flags, int *pOutFlags);
	int (*xDelete) (sqlite3_vfs *, const char *zName, int syncDir);
	int (*xAccess) (sqlite3_vfs *, const char *zName, int flags,
			int *pResOut);
	int (*xFullPathname) (sqlite3_vfs *, const char *zName,
			      int nOut, char *zOut);
	void *(*xDlOpen) (sqlite3_vfs *, const char *zFilename);
	void (*xDlError) (sqlite3_vfs *, int nByte, char *zErrMsg);
	void (*(*xDlSym) (sqlite3_vfs *, void *, const char *zSymbol))
	(void);
	void (*xDlClose) (sqlite3_vfs *, void *);
	int (*xRandomness) (sqlite3_vfs *, int nByte, char *zOut);
	int (*xSleep) (sqlite3_vfs *, int microseconds);
	int (*xCurrentTime) (sqlite3_vfs *, double *);
	int (*xGetLastError) (sqlite3_vfs *, int, char *);
	/*
	** The methods above are in version 1 of the sqlite_vfs object
	** definition.  Those that follow are added in version 2 or later
	*/
	int (*xCurrentTimeInt64) (sqlite3_vfs *, sqlite3_int64 *);
	/*
	** The methods above are in versions 1 and 2 of the sqlite_vfs object.
	** Those below are for version 3 and greater.
	*/
	int (*xSetSystemCall) (sqlite3_vfs *, const char *zName,
			       sqlite3_syscall_ptr);
	sqlite3_syscall_ptr(*xGetSystemCall) (sqlite3_vfs *,
					      const char *zName);
	const char *(*xNextSystemCall) (sqlite3_vfs *,
					const char *zName);
	/*
	** The methods above are in versions 1 through 3 of the sqlite_vfs object.
	** New fields may be appended in future versions.  The iVersion
	** value will increment whenever this happens.
	*/
};

#define SQLITE_LIMIT_LENGTH                    0
#define SQLITE_LIMIT_SQL_LENGTH                1
#define SQLITE_LIMIT_COLUMN                    2
#define SQLITE_LIMIT_EXPR_DEPTH                3
#define SQLITE_LIMIT_COMPOUND_SELECT           4
#define SQLITE_LIMIT_VDBE_OP                   5
#define SQLITE_LIMIT_FUNCTION_ARG              6
#define SQLITE_LIMIT_ATTACHED                  7
#define SQLITE_LIMIT_LIKE_PATTERN_LENGTH       8
#define SQLITE_LIMIT_TRIGGER_DEPTH             9
#define SQLITE_LIMIT_WORKER_THREADS           10

enum sql_ret_code {
	/** Result of a routine is ok. */
	SQLITE_OK = 0,
	/** Common error code. */
	SQLITE_ERROR,
	/** Access permission denied. */
	SQLITE_PERM,
	/** Callback routine requested an abort. */
	SQLITE_ABORT,
	/** The database file is locked. */
	SQLITE_BUSY,
	/** A table in the database is locked. */
	SQLITE_LOCKED,
	/** A malloc() failed. */
	SQLITE_NOMEM,
	/** Operation terminated by sqlite3_interrupt(). */
	SQLITE_INTERRUPT,
	/** Some kind of disk I/O error occurred. */
	SQLITE_IOERR,
	/** The database disk image is malformed. */
	SQLITE_CORRUPT,
	/** Unknown opcode in sqlite3_file_control(). */
	SQLITE_NOTFOUND,
	/** Insertion failed because database is full. */
	SQLITE_FULL,
	/** Unable to open the database file. */
	SQLITE_CANTOPEN,
	/** The database schema changed. */
	SQLITE_SCHEMA,
	/** String or BLOB exceeds size limit. */
	SQLITE_TOOBIG,
	/** Abort due to constraint violation. */
	SQLITE_CONSTRAINT,
	/** Data type mismatch. */
	SQLITE_MISMATCH,
	/** Library used incorrectly. */
	SQLITE_MISUSE,
	/** 2nd parameter to sqlite3_bind out of range. */
	SQLITE_RANGE,
	SQL_TARANTOOL_ITERATOR_FAIL,
	SQL_TARANTOOL_INSERT_FAIL,
	SQL_TARANTOOL_DELETE_FAIL,
	SQL_TARANTOOL_ERROR,
	/** Warnings from sqlite3_log(). */
	SQLITE_WARNING,
	/** sqlite3_step() has another row ready. */
	SQLITE_ROW,
	/** sqlite3_step() has finished executing. */
	SQLITE_DONE,
};

void *
sqlite3_malloc(int);

void *
sqlite3_malloc64(sqlite3_uint64);

void *
sqlite3_realloc(void *, int);

void *
sqlite3_realloc64(void *, sqlite3_uint64);

void
sqlite3_free(void *);

sqlite3_uint64
sqlite3_msize(void *);

int
sqlite3_stricmp(const char *, const char *);

int
sqlite3_strnicmp(const char *, const char *, int);

 const void *
sqlite3_value_blob(sqlite3_value *);

int
sqlite3_value_bytes(sqlite3_value *);

double
sqlite3_value_double(sqlite3_value *);

int
sqlite3_value_int(sqlite3_value *);

sqlite3_int64
sqlite3_value_int64(sqlite3_value *);

const unsigned char *
sqlite3_value_text(sqlite3_value *);

int
sqlite3_value_type(sqlite3_value *);

int
sqlite3_value_numeric_type(sqlite3_value *);

sqlite3 *
sqlite3_context_db_handle(sqlite3_context *);


void
sqlite3_result_blob(sqlite3_context *, const void *,
		    int, void (*)(void *));

void
sqlite3_result_blob64(sqlite3_context *, const void *,
		      sqlite3_uint64, void (*)(void *));

void
sqlite3_result_double(sqlite3_context *, double);

void
sqlite3_result_error(sqlite3_context *, const char *,
		     int);

void
sqlite3_result_error_toobig(sqlite3_context *);

void
sqlite3_result_error_nomem(sqlite3_context *);

void
sqlite3_result_error_code(sqlite3_context *, int);

void
sqlite3_result_int(sqlite3_context *, int);

void
sqlite3_result_int64(sqlite3_context *, sqlite3_int64);

void
sqlite3_result_null(sqlite3_context *);

void
sqlite3_result_text(sqlite3_context *, const char *,
		    int, void (*)(void *));

void
sqlite3_result_text64(sqlite3_context *, const char *,
		      sqlite3_uint64, void (*)(void *));

void
sqlite3_result_value(sqlite3_context *,
		     sqlite3_value *);

void
sqlite3_result_zeroblob(sqlite3_context *, int n);

int
sqlite3_result_zeroblob64(sqlite3_context *,
			  sqlite3_uint64 n);

char *
sqlite3_mprintf(const char *, ...);
char *
sqlite3_vmprintf(const char *, va_list);
char *
sqlite3_snprintf(int, char *, const char *, ...);
char *
sqlite3_vsnprintf(int, char *, const char *, va_list);

int
sqlite3_strlike(const char *zGlob, const char *zStr,
		unsigned int cEsc);

typedef void (*sqlite3_destructor_type) (void *);
#define SQLITE_STATIC      ((sqlite3_destructor_type)0)
#define SQLITE_TRANSIENT   ((sqlite3_destructor_type)-1)

int
sqlite3_strglob(const char *zGlob, const char *zStr);

int
sqlite3_prepare(sqlite3 * db,	/* Database handle */
		const char *zSql,	/* SQL statement, UTF-8 encoded */
		int nByte,	/* Maximum length of zSql in bytes. */
		sqlite3_stmt ** ppStmt,	/* OUT: Statement handle */
		const char **pzTail	/* OUT: Pointer to unused portion of zSql */
	);

int
sqlite3_prepare_v2(sqlite3 * db,	/* Database handle */
		   const char *zSql,	/* SQL statement, UTF-8 encoded */
		   int nByte,	/* Maximum length of zSql in bytes. */
		   sqlite3_stmt ** ppStmt,	/* OUT: Statement handle */
		   const char **pzTail	/* OUT: Pointer to unused portion of zSql */
	);

int
sqlite3_step(sqlite3_stmt *);

const void *
sqlite3_column_blob(sqlite3_stmt *, int iCol);

int
sqlite3_column_bytes(sqlite3_stmt *, int iCol);

int
sqlite3_column_bytes16(sqlite3_stmt *, int iCol);

double
sqlite3_column_double(sqlite3_stmt *, int iCol);

int
sqlite3_column_int(sqlite3_stmt *, int iCol);

sqlite3_int64
sqlite3_column_int64(sqlite3_stmt *, int iCol);

const unsigned char *
sqlite3_column_text(sqlite3_stmt *,
		    int iCol);

int
sqlite3_column_type(sqlite3_stmt *, int iCol);

sqlite3_value *
sqlite3_column_value(sqlite3_stmt *,
		     int iCol);

int
sqlite3_finalize(sqlite3_stmt * pStmt);

int
sqlite3_exec(sqlite3 *,	/* An open database */
	     const char *sql,	/* SQL to be evaluated */
	     int (*callback) (void *, int, char **, char **),	/* Callback function */
	     void *,	/* 1st argument to callback */
	     char **errmsg	/* Error msg written here */
	);
#define SQLITE_IOERR_READ              (SQLITE_IOERR | (1<<8))
#define SQLITE_IOERR_SHORT_READ        (SQLITE_IOERR | (2<<8))
#define SQLITE_IOERR_WRITE             (SQLITE_IOERR | (3<<8))
#define SQLITE_IOERR_FSYNC             (SQLITE_IOERR | (4<<8))
#define SQLITE_IOERR_DIR_FSYNC         (SQLITE_IOERR | (5<<8))
#define SQLITE_IOERR_TRUNCATE          (SQLITE_IOERR | (6<<8))
#define SQLITE_IOERR_FSTAT             (SQLITE_IOERR | (7<<8))
#define SQLITE_IOERR_UNLOCK            (SQLITE_IOERR | (8<<8))
#define SQLITE_IOERR_RDLOCK            (SQLITE_IOERR | (9<<8))
#define SQLITE_IOERR_DELETE            (SQLITE_IOERR | (10<<8))
#define SQLITE_IOERR_BLOCKED           (SQLITE_IOERR | (11<<8))
#define SQLITE_IOERR_NOMEM             (SQLITE_IOERR | (12<<8))
#define SQLITE_IOERR_ACCESS            (SQLITE_IOERR | (13<<8))
#define SQLITE_IOERR_CHECKRESERVEDLOCK (SQLITE_IOERR | (14<<8))
#define SQLITE_IOERR_LOCK              (SQLITE_IOERR | (15<<8))
#define SQLITE_IOERR_CLOSE             (SQLITE_IOERR | (16<<8))
#define SQLITE_IOERR_DIR_CLOSE         (SQLITE_IOERR | (17<<8))
#define SQLITE_IOERR_SHMOPEN           (SQLITE_IOERR | (18<<8))
#define SQLITE_IOERR_SHMSIZE           (SQLITE_IOERR | (19<<8))
#define SQLITE_IOERR_SHMLOCK           (SQLITE_IOERR | (20<<8))
#define SQLITE_IOERR_SHMMAP            (SQLITE_IOERR | (21<<8))
#define SQLITE_IOERR_SEEK              (SQLITE_IOERR | (22<<8))
#define SQLITE_IOERR_DELETE_NOENT      (SQLITE_IOERR | (23<<8))
#define SQLITE_IOERR_MMAP              (SQLITE_IOERR | (24<<8))
#define SQLITE_IOERR_GETTEMPPATH       (SQLITE_IOERR | (25<<8))
#define SQLITE_IOERR_CONVPATH          (SQLITE_IOERR | (26<<8))
#define SQLITE_IOERR_VNODE             (SQLITE_IOERR | (27<<8))
#define SQLITE_ABORT_ROLLBACK          (SQLITE_ABORT | (2<<8))
#define SQLITE_CONSTRAINT_CHECK        (SQLITE_CONSTRAINT | (1<<8))
#define SQLITE_CONSTRAINT_FOREIGNKEY   (SQLITE_CONSTRAINT | (3<<8))
#define SQLITE_CONSTRAINT_FUNCTION     (SQLITE_CONSTRAINT | (4<<8))
#define SQLITE_CONSTRAINT_NOTNULL      (SQLITE_CONSTRAINT | (5<<8))
#define SQLITE_CONSTRAINT_PRIMARYKEY   (SQLITE_CONSTRAINT | (6<<8))
#define SQLITE_CONSTRAINT_TRIGGER      (SQLITE_CONSTRAINT | (7<<8))
#define SQLITE_CONSTRAINT_UNIQUE       (SQLITE_CONSTRAINT | (8<<8))

enum sql_type {
	SQLITE_INTEGER = 1,
	SQLITE_FLOAT = 2,
	SQLITE_TEXT = 3,
	SQLITE_BLOB = 4,
	SQLITE_NULL = 5,
};

/**
 * Structure for internal usage during INSERT/UPDATE
 * statements compilation.
 */
struct on_conflict {
	/**
	 * Represents an error action in queries like
	 * INSERT/UPDATE OR <override_error>, which
	 * overrides all space constraints error actions.
	 * That kind of error action is strictly specified by
	 * user and therefore have highest priority.
	 */
	enum on_conflict_action override_error;
	/**
	 * Represents an ON CONFLICT action which can be
	 * optimized and executed without VDBE bytecode, by
	 * Tarantool facilities. If optimization is not available,
	 * then the value is ON_CONFLICT_ACTION_NONE, otherwise
	 * it is ON_CONFLICT_ACTON_IGNORE or
	 * ON_CONFLICT_ACTION_REPLACE.
	 */
	enum on_conflict_action optimized_action;
};

void *
sqlite3_user_data(sqlite3_context *);

void
sqlite3_randomness(int N, void *P);

int
sqlite3_changes(sqlite3 *);

int
sqlite3_total_changes(sqlite3 *);

void *
sqlite3_user_data(sqlite3_context *);

const char *
sqlite3_libversion(void);

const char *
sqlite3_sourceid(void);

void
sqlite3_log(int iErrCode, const char *zFormat, ...);

void *
sqlite3_aggregate_context(sqlite3_context *,
			  int nBytes);


int
sqlite3_column_count(sqlite3_stmt * pStmt);

const char *
sqlite3_column_name(sqlite3_stmt *, int N);

const char *
sqlite3_errmsg(sqlite3 *);

int
sqlite3_initialize(void);

#define SQLITE_VERSION        "3.16.2"
#define SQLITE_VERSION_NUMBER 3016002
#define SQLITE_SOURCE_ID      "D 1970-01-01 00:00:00 00000000-0000-0000-0000-000000000000"

int
sqlite3_os_end(void);

#define SQLITE_CONFIG_SCRATCH       6	/* void*, int sz, int N */
#define SQLITE_CONFIG_MEMSTATUS     9	/* boolean */
#define SQLITE_CONFIG_LOOKASIDE    13	/* int int */
#define SQLITE_CONFIG_LOG          16	/* xFunc, void* */
#define SQLITE_CONFIG_URI          17	/* int */
#define SQLITE_CONFIG_COVERING_INDEX_SCAN 20	/* int */
#define SQLITE_CONFIG_SQLLOG       21	/* xSqllog, void* */
#define SQLITE_CONFIG_MMAP_SIZE    22	/* sqlite3_int64, sqlite3_int64 */
#define SQLITE_CONFIG_PMASZ               24	/* unsigned int szPma */
#define SQLITE_CONFIG_STMTJRNL_SPILL      25	/* int nByte */

#define SQLITE_DBCONFIG_LOOKASIDE             1001	/* void* int int */
#define SQLITE_DBCONFIG_ENABLE_FKEY           1002	/* int int* */
#define SQLITE_DBCONFIG_ENABLE_TRIGGER        1003	/* int int* */
#define SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE      1006	/* int int* */

#define SQLITE_TRACE_STMT       0x01
#define SQLITE_TRACE_PROFILE    0x02
#define SQLITE_TRACE_ROW        0x04
#define SQLITE_TRACE_CLOSE      0x08

#define SQLITE_DETERMINISTIC    0x800

#define SQLITE_STATUS_MEMORY_USED          0
#define SQLITE_STATUS_PAGECACHE_USED       1
#define SQLITE_STATUS_PAGECACHE_OVERFLOW   2
#define SQLITE_STATUS_SCRATCH_USED         3
#define SQLITE_STATUS_SCRATCH_OVERFLOW     4
#define SQLITE_STATUS_MALLOC_SIZE          5
#define SQLITE_STATUS_PARSER_STACK         6
#define SQLITE_STATUS_PAGECACHE_SIZE       7
#define SQLITE_STATUS_SCRATCH_SIZE         8
#define SQLITE_STATUS_MALLOC_COUNT         9

sqlite3_int64
sqlite3_memory_used(void);

int
sqlite3_create_function_v2(sqlite3 * db,
			   const char *zFunctionName,
			   int nArg,
			   int flags,
			   void *pApp,
			   void (*xFunc) (sqlite3_context *,
					  int,
					  sqlite3_value **),
			   void (*xStep) (sqlite3_context *,
					  int,
					  sqlite3_value **),
			   void (*xFinal)
			   (sqlite3_context *),
			   void (*xDestroy) (void *)
	);

#define SQLITE_OPEN_READONLY         0x00000001	/* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_READWRITE        0x00000002	/* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_CREATE           0x00000004	/* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_DELETEONCLOSE    0x00000008	/* VFS only */
#define SQLITE_OPEN_EXCLUSIVE        0x00000010	/* VFS only */
#define SQLITE_OPEN_AUTOPROXY        0x00000020	/* VFS only */
#define SQLITE_OPEN_URI              0x00000040	/* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_MEMORY           0x00000080	/* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_MAIN_DB          0x00000100	/* VFS only */
#define SQLITE_OPEN_TEMP_DB          0x00000200	/* VFS only */
#define SQLITE_OPEN_SHAREDCACHE      0x00020000	/* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_PRIVATECACHE     0x00040000	/* Ok for sqlite3_open_v2() */

sqlite3_vfs *
sqlite3_vfs_find(const char *zVfsName);

#define SQLITE_TESTCTRL_FIRST                    5
#define SQLITE_TESTCTRL_PRNG_SAVE                5
#define SQLITE_TESTCTRL_PRNG_RESTORE             6
#define SQLITE_TESTCTRL_PRNG_RESET               7
#define SQLITE_TESTCTRL_BITVEC_TEST              8
#define SQLITE_TESTCTRL_FAULT_INSTALL            9
#define SQLITE_TESTCTRL_BENIGN_MALLOC_HOOKS     10
#define SQLITE_TESTCTRL_PENDING_BYTE            11
#define SQLITE_TESTCTRL_ASSERT                  12
#define SQLITE_TESTCTRL_ALWAYS                  13
#define SQLITE_TESTCTRL_RESERVE                 14
#define SQLITE_TESTCTRL_OPTIMIZATIONS           15
#define SQLITE_TESTCTRL_ISKEYWORD               16
#define SQLITE_TESTCTRL_SCRATCHMALLOC           17
#define SQLITE_TESTCTRL_LOCALTIME_FAULT         18
#define SQLITE_TESTCTRL_EXPLAIN_STMT            19	/* NOT USED */
#define SQLITE_TESTCTRL_ONCE_RESET_THRESHOLD    19
#define SQLITE_TESTCTRL_NEVER_CORRUPT           20
#define SQLITE_TESTCTRL_VDBE_COVERAGE           21
#define SQLITE_TESTCTRL_BYTEORDER               22
#define SQLITE_TESTCTRL_ISINIT                  23
#define SQLITE_TESTCTRL_SORTER_MMAP             24
#define SQLITE_TESTCTRL_IMPOSTER                25
#define SQLITE_TESTCTRL_LAST                    25

int
sqlite3_status64(int op, sqlite3_int64 * pCurrent,
		 sqlite3_int64 * pHighwater,
		 int resetFlag);

int
sqlite3_config(int, ...);


typedef struct sqlite3_io_methods sqlite3_io_methods;
struct sqlite3_io_methods {
	int iVersion;
	int (*xClose) (sqlite3_file *);
	int (*xRead) (sqlite3_file *, void *, int iAmt,
		      sqlite3_int64 iOfst);
	int (*xWrite) (sqlite3_file *, const void *, int iAmt,
		       sqlite3_int64 iOfst);
	int (*xTruncate) (sqlite3_file *, sqlite3_int64 size);
	int (*xSync) (sqlite3_file *, int flags);
	int (*xFileSize) (sqlite3_file *, sqlite3_int64 * pSize);
	int (*xLock) (sqlite3_file *, int);
	int (*xUnlock) (sqlite3_file *, int);
	int (*xCheckReservedLock) (sqlite3_file *, int *pResOut);
	int (*xFileControl) (sqlite3_file *, int op, void *pArg);
	int (*xSectorSize) (sqlite3_file *);
	int (*xDeviceCharacteristics) (sqlite3_file *);
	/* Methods above are valid for version 1 */
	int (*xShmMap) (sqlite3_file *, int iPg, int pgsz, int,
			void volatile **);
	int (*xShmLock) (sqlite3_file *, int offset, int n, int flags);
	void (*xShmBarrier) (sqlite3_file *);
	int (*xShmUnmap) (sqlite3_file *, int deleteFlag);
	/* Methods above are valid for version 2 */
	int (*xFetch) (sqlite3_file *, sqlite3_int64 iOfst, int iAmt,
		       void **pp);
	int (*xUnfetch) (sqlite3_file *, sqlite3_int64 iOfst, void *p);
	/* Methods above are valid for version 3 */
	/* Additional methods may be added in future releases */
};

#define SQLITE_FCNTL_LOCKSTATE               1
#define SQLITE_FCNTL_GET_LOCKPROXYFILE       2
#define SQLITE_FCNTL_SET_LOCKPROXYFILE       3
#define SQLITE_FCNTL_LAST_ERRNO              4
#define SQLITE_FCNTL_SIZE_HINT               5
#define SQLITE_FCNTL_CHUNK_SIZE              6
#define SQLITE_FCNTL_FILE_POINTER            7
#define SQLITE_FCNTL_SYNC_OMITTED            8
#define SQLITE_FCNTL_OVERWRITE              10
#define SQLITE_FCNTL_VFSNAME                11
#define SQLITE_FCNTL_POWERSAFE_OVERWRITE    12
#define SQLITE_FCNTL_PRAGMA                 13
#define SQLITE_FCNTL_BUSYHANDLER            14
#define SQLITE_FCNTL_TEMPFILENAME           15
#define SQLITE_FCNTL_MMAP_SIZE              16
#define SQLITE_FCNTL_TRACE                  17
#define SQLITE_FCNTL_HAS_MOVED              18
#define SQLITE_FCNTL_SYNC                   19
#define SQLITE_FCNTL_COMMIT_PHASETWO        20
#define SQLITE_FCNTL_ZIPVFS                 22
#define SQLITE_FCNTL_RBU                    23
#define SQLITE_FCNTL_VFS_POINTER            24
#define SQLITE_FCNTL_PDB                    26

int
sqlite3_os_init(void);

int
sqlite3_db_release_memory(sqlite3 *);

int
sqlite3_busy_timeout(sqlite3 *, int ms);

sqlite3_int64
sqlite3_soft_heap_limit64(sqlite3_int64 N);

int
sqlite3_limit(sqlite3 *, int id, int newVal);

#define SQLITE_SYNC_NORMAL        0x00002
#define SQLITE_SYNC_FULL          0x00003
#define SQLITE_SYNC_DATAONLY      0x00010

int
sqlite3_uri_boolean(const char *zFile,
		    const char *zParam, int bDefault);

extern char *
sqlite3_temp_directory;

const char *
sqlite3_uri_parameter(const char *zFilename,
		      const char *zParam);

#define SQLITE_ACCESS_EXISTS    0
#define SQLITE_ACCESS_READWRITE 1	/* Used by PRAGMA temp_store_directory */
#define SQLITE_ACCESS_READ      2	/* Unused */

#define SQLITE_DBSTATUS_LOOKASIDE_USED       0
#define SQLITE_DBSTATUS_CACHE_USED           1
#define SQLITE_DBSTATUS_SCHEMA_USED          2
#define SQLITE_DBSTATUS_STMT_USED            3
#define SQLITE_DBSTATUS_LOOKASIDE_HIT        4
#define SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE  5
#define SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL  6
#define SQLITE_DBSTATUS_CACHE_HIT            7
#define SQLITE_DBSTATUS_CACHE_MISS           8
#define SQLITE_DBSTATUS_CACHE_WRITE          9
#define SQLITE_DBSTATUS_DEFERRED_FKS        10
#define SQLITE_DBSTATUS_CACHE_USED_SHARED   11
#define SQLITE_DBSTATUS_MAX                 11	/* Largest defined DBSTATUS */

const char *
sqlite3_sql(sqlite3_stmt * pStmt);

int
sqlite3_vfs_register(sqlite3_vfs *, int makeDflt);

void
sqlite3_free_table(char **result);

#define SQLITE_STMTSTATUS_FULLSCAN_STEP     1
#define SQLITE_STMTSTATUS_SORT              2
#define SQLITE_STMTSTATUS_AUTOINDEX         3
#define SQLITE_STMTSTATUS_VM_STEP           4

void
sqlite3_interrupt(sqlite3 *);

int
sqlite3_bind_blob(sqlite3_stmt *, int, const void *,
		  int n, void (*)(void *));

int
sqlite3_bind_blob64(sqlite3_stmt *, int, const void *,
		    sqlite3_uint64, void (*)(void *));

int
sqlite3_bind_double(sqlite3_stmt *, int, double);

int
sqlite3_bind_int(sqlite3_stmt *, int, int);

int
sqlite3_bind_int64(sqlite3_stmt *, int, sqlite3_int64);

int
sqlite3_bind_null(sqlite3_stmt *, int);

int
sqlite3_bind_text(sqlite3_stmt *, int, const char *, int,
		  void (*)(void *));

int
sqlite3_bind_text64(sqlite3_stmt *, int, const char *,
		    sqlite3_uint64, void (*)(void *));
int
sqlite3_bind_value(sqlite3_stmt *, int,
		   const sqlite3_value *);

int
sqlite3_bind_zeroblob(sqlite3_stmt *, int, int n);

int
sqlite3_bind_zeroblob64(sqlite3_stmt *, int,
			sqlite3_uint64);

int
sqlite3_stmt_busy(sqlite3_stmt *);

int
sql_init_db(sqlite3 **db);

int
sqlite3_close(sqlite3 *);


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
sqlite3_bind_parameter_lindex(sqlite3_stmt * pStmt, const char *zName,
			      int nName);

/*
 * If compiling for a processor that lacks floating point support,
 * substitute integer for floating-point
 */
#ifdef SQLITE_OMIT_FLOATING_POINT
#define double sqlite_int64
#define float sqlite_int64
#define LONGDOUBLE_TYPE sqlite_int64
#ifndef SQLITE_BIG_DBL
#define SQLITE_BIG_DBL (((sqlite3_int64)1)<<50)
#endif
#define SQLITE_OMIT_DATETIME_FUNCS 1
#define SQLITE_OMIT_TRACE 1
#undef SQLITE_MIXED_ENDIAN_64BIT_FLOAT
#undef SQLITE_HAVE_ISNAN
#endif
#ifndef SQLITE_BIG_DBL
#define SQLITE_BIG_DBL (1e99)
#endif

/*
 * OMIT_TEMPDB is set to 1 if SQLITE_OMIT_TEMPDB is defined, or 0
 * afterward. Having this macro allows us to cause the C compiler
 * to omit code used by TEMP tables without messy #ifndef statements.
 */
#ifdef SQLITE_OMIT_TEMPDB
#define OMIT_TEMPDB 1
#else
#define OMIT_TEMPDB 0
#endif

/*
 * Determine whether triggers are recursive by default.  This can be
 * changed at run-time using a pragma.
 */
#ifndef SQLITE_DEFAULT_RECURSIVE_TRIGGERS
#define SQLITE_DEFAULT_RECURSIVE_TRIGGERS 0
#endif

/*
 * Provide a default value for SQLITE_TEMP_STORE in case it is not specified
 * on the command-line
 */
#ifndef SQLITE_TEMP_STORE
#define SQLITE_TEMP_STORE 1
#define SQLITE_TEMP_STORE_xc 1	/* Exclude from ctime.c */
#endif

/*
 * If no value has been provided for SQLITE_MAX_WORKER_THREADS, or if
 * SQLITE_TEMP_STORE is set to 3 (never use temporary files), set it
 * to zero.
 */
#if SQLITE_TEMP_STORE==3
#undef SQLITE_MAX_WORKER_THREADS
#define SQLITE_MAX_WORKER_THREADS 0
#endif
#ifndef SQLITE_MAX_WORKER_THREADS
#define SQLITE_MAX_WORKER_THREADS 8
#endif
#ifndef SQLITE_DEFAULT_WORKER_THREADS
#define SQLITE_DEFAULT_WORKER_THREADS 0
#endif
#if SQLITE_DEFAULT_WORKER_THREADS>SQLITE_MAX_WORKER_THREADS
#undef SQLITE_MAX_WORKER_THREADS
#define SQLITE_MAX_WORKER_THREADS SQLITE_DEFAULT_WORKER_THREADS
#endif

/*
 * The default initial allocation for the pagecache when using separate
 * pagecaches for each database connection.  A positive number is the
 * number of pages.  A negative number N translations means that a buffer
 * of -1024*N bytes is allocated and used for as many pages as it will hold.
 */
#ifndef SQLITE_DEFAULT_PCACHE_INITSZ
#define SQLITE_DEFAULT_PCACHE_INITSZ 100
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
typedef sqlite_int64 i64;	/* 8-byte signed integer */
typedef sqlite_uint64 u64;	/* 8-byte unsigned integer */
typedef UINT32_TYPE u32;	/* 4-byte unsigned integer */
typedef UINT16_TYPE u16;	/* 2-byte unsigned integer */
typedef INT16_TYPE i16;		/* 2-byte signed integer */
typedef UINT8_TYPE u8;		/* 1-byte unsigned integer */
typedef INT8_TYPE i8;		/* 1-byte signed integer */

/*
 * SQLITE_MAX_U32 is a u64 constant that is the maximum u64 value
 * that can be stored in a u32 without loss of data.  The value
 * is 0x00000000ffffffff.  But because of quirks of some compilers, we
 * have to specify the value in the less intuitive manner shown:
 */
#define SQLITE_MAX_U32  ((((u64)1)<<32)-1)

/*
 * The datatype used to store estimates of the number of rows in a
 * table or index.  This is an unsigned integer type.  For 99.9% of
 * the world, a 32-bit integer is sufficient.  But a 64-bit integer
 * can be used at compile-time if desired.
 */
#ifdef SQLITE_64BIT_STATS
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
 * Set the SQLITE_PTRSIZE macro to the number of bytes in a pointer
 */
#ifndef SQLITE_PTRSIZE
#if defined(__SIZEOF_POINTER__)
#define SQLITE_PTRSIZE __SIZEOF_POINTER__
#elif defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
       defined(_M_ARM)   || defined(__arm__)    || defined(__x86)
#define SQLITE_PTRSIZE 4
#else
#define SQLITE_PTRSIZE 8
#endif
#endif

/* The uptr type is an unsigned integer large enough to hold a pointer
 */
#if defined(HAVE_STDINT_H)
typedef uintptr_t uptr;
#elif SQLITE_PTRSIZE==4
typedef u32 uptr;
#else
typedef u64 uptr;
#endif

/*
 * The SQLITE_WITHIN(P,S,E) macro checks to see if pointer P points to
 * something between S (inclusive) and E (exclusive).
 *
 * In other words, S is a buffer and E is a pointer to the first byte after
 * the end of buffer S.  This macro returns true if P points to something
 * contained within the buffer S.
 */
#define SQLITE_WITHIN(P,S,E) (((uptr)(P)>=(uptr)(S))&&((uptr)(P)<(uptr)(E)))

/*
 * Macros to determine whether the machine is big or little endian,
 * and whether or not that determination is run-time or compile-time.
 *
 * For best performance, an attempt is made to guess at the byte-order
 * using C-preprocessor macros.  If that is unsuccessful, or if
 * -DSQLITE_RUNTIME_BYTEORDER=1 is set, then byte-order is determined
 * at run-time.
 */
#if (defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
     defined(__x86_64) || defined(__x86_64__) || defined(_M_X64)  ||    \
     defined(_M_AMD64) || defined(_M_ARM)     || defined(__x86)   ||    \
     defined(__arm__)) && !defined(SQLITE_RUNTIME_BYTEORDER)
#define SQLITE_BYTEORDER    1234
#define SQLITE_BIGENDIAN    0
#define SQLITE_LITTLEENDIAN 1
#endif
#if (defined(sparc)    || defined(__ppc__))  \
    && !defined(SQLITE_RUNTIME_BYTEORDER)
#define SQLITE_BYTEORDER    4321
#define SQLITE_BIGENDIAN    1
#define SQLITE_LITTLEENDIAN 0
#endif
#if !defined(SQLITE_BYTEORDER)
#ifdef SQLITE_AMALGAMATION
const int sqlite3one = 1;
#else
extern const int sqlite3one;
#endif
#define SQLITE_BYTEORDER    0	/* 0 means "unknown at compile-time" */
#define SQLITE_BIGENDIAN    (*(char *)(&sqlite3one)==0)
#define SQLITE_LITTLEENDIAN (*(char *)(&sqlite3one)==1)
#define SQLITE_UTF16NATIVE  (SQLITE_BIGENDIAN?SQLITE_UTF16BE:SQLITE_UTF16LE)
#endif

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
 * Except, if SQLITE_4_BYTE_ALIGNED_MALLOC is defined, then the
 * underlying malloc() implementation might return us 4-byte aligned
 * pointers.  In that case, only verify 4-byte alignment.
 */
#ifdef SQLITE_4_BYTE_ALIGNED_MALLOC
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
#ifndef SQLITE_MAX_MMAP_SIZE
#if defined(__linux__) \
  || (defined(__APPLE__) && defined(__MACH__))
#define SQLITE_MAX_MMAP_SIZE 0x7fff0000	/* 2147418112 */
#else
#define SQLITE_MAX_MMAP_SIZE 0
#endif
#define SQLITE_MAX_MMAP_SIZE_xc 1	/* exclude from ctime.c */
#endif

/*
 * The default MMAP_SIZE is zero on all platforms.  Or, even if a larger
 * default MMAP_SIZE is specified at compile-time, make sure that it does
 * not exceed the maximum mmap size.
 */
#ifndef SQLITE_DEFAULT_MMAP_SIZE
#define SQLITE_DEFAULT_MMAP_SIZE 0
#define SQLITE_DEFAULT_MMAP_SIZE_xc 1	/* Exclude from ctime.c */
#endif
#if SQLITE_DEFAULT_MMAP_SIZE>SQLITE_MAX_MMAP_SIZE
#undef SQLITE_DEFAULT_MMAP_SIZE
#define SQLITE_DEFAULT_MMAP_SIZE SQLITE_MAX_MMAP_SIZE
#endif

/*
 * SELECTTRACE_ENABLED will be either 1 or 0 depending on whether or not
 * the Select query generator tracing logic is turned on.
 */
#if defined(SQLITE_DEBUG) || defined(SQLITE_ENABLE_SELECTTRACE)
#define SELECTTRACE_ENABLED
#else
#undef SELECTTRACE_ENABLED
#endif

#if defined(SQLITE_DEBUG) || defined(SQLITE_ENABLE_WHERETRACE)
#define WHERETRACE_ENABLED
#else
#undef WHERETRACE_ENABLED
#endif

/*
 * An instance of the following structure is used to store the busy-handler
 * callback for a given sqlite handle.
 *
 * The sqlite.busyHandler member of the sqlite struct contains the busy
 * callback for the database handle. Each pager opened via the sqlite
 * handle is passed a pointer to sqlite.busyHandler. The busy-handler
 * callback is currently invoked only from within pager.c.
 */
typedef struct BusyHandler BusyHandler;
struct BusyHandler {
	int (*xFunc) (void *, int);	/* The busy callback */
	void *pArg;		/* First arg to busy callback */
	int nBusy;		/* Incremented with each busy call */
};

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
 * The following value as a destructor means to use sqlite3DbFree().
 * The sqlite3DbFree() routine requires two parameters instead of the
 * one parameter that destructors normally want.  So we have to introduce
 * this magic value that the code knows to handle differently.  Any
 * pointer will work here as long as it is distinct from SQLITE_STATIC
 * and SQLITE_TRANSIENT.
 */
#define SQLITE_DYNAMIC   ((sqlite3_destructor_type)sqlite3MallocSize)

/*
 * When SQLITE_OMIT_WSD is defined, it means that the target platform does
 * not support Writable Static Data (WSD) such as global and static variables.
 * All variables must either be on the stack or dynamically allocated from
 * the heap.  When WSD is unsupported, the variable declarations scattered
 * throughout the SQLite code must become constants instead.  The SQLITE_WSD
 * macro is used for this purpose.  And instead of referencing the variable
 * directly, we use its constant as a key to lookup the run-time allocated
 * buffer that holds real variable.  The constant is also the initializer
 * for the run-time allocated buffer.
 *
 * In the usual case where WSD is supported, the SQLITE_WSD and GLOBAL
 * macros become no-ops and have zero performance impact.
 */
#ifdef SQLITE_OMIT_WSD
#define SQLITE_WSD const
#define GLOBAL(t,v) (*(t*)sqlite3_wsd_find((void*)&(v), sizeof(v)))
#define sqlite3GlobalConfig GLOBAL(struct Sqlite3Config, sqlite3Config)
int sqlite3_wsd_init(int N, int J);
void *sqlite3_wsd_find(void *K, int L);
#else
#define SQLITE_WSD
#define GLOBAL(t,v) v
#define sqlite3GlobalConfig sqlite3Config
#endif

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
typedef struct Schema Schema;
typedef struct Expr Expr;
typedef struct ExprList ExprList;
typedef struct ExprSpan ExprSpan;
typedef struct FKey FKey;
typedef struct FuncDestructor FuncDestructor;
typedef struct FuncDef FuncDef;
typedef struct FuncDefHash FuncDefHash;
typedef struct IdList IdList;
typedef struct Index Index;
typedef struct KeyClass KeyClass;
typedef struct Lookaside Lookaside;
typedef struct LookasideSlot LookasideSlot;
typedef struct NameContext NameContext;
typedef struct Parse Parse;
typedef struct PreUpdate PreUpdate;
typedef struct PrintfArguments PrintfArguments;
typedef struct RowSet RowSet;
typedef struct Savepoint Savepoint;
typedef struct Select Select;
typedef struct SQLiteThread SQLiteThread;
typedef struct SelectDest SelectDest;
typedef struct SrcList SrcList;
typedef struct StrAccum StrAccum;
typedef struct Table Table;
typedef struct Token Token;
typedef struct TreeView TreeView;
typedef struct Trigger Trigger;
typedef struct TriggerPrg TriggerPrg;
typedef struct TriggerStep TriggerStep;
typedef struct UnpackedRecord UnpackedRecord;
typedef struct Walker Walker;
typedef struct WhereInfo WhereInfo;
typedef struct With With;

/* A VList object records a mapping between parameters/variables/wildcards
 * in the SQL statement (such as $abc, @pqr, or :xyz) and the integer
 * variable number associated with that parameter.  See the format description
 * on the sqlite3VListAdd() routine for more information.  A VList is really
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
 * An instance of the following structure stores a database schema.
 */
struct Schema {
	int schema_cookie;      /* Database schema version number for this file */
	Hash tblHash;		/* All tables indexed by name */
	Hash trigHash;		/* All triggers indexed by name */
	Hash fkeyHash;		/* All foreign keys by referenced table name */
};

/*
 * The number of different kinds of things that can be limited
 * using the sqlite3_limit() interface.
 */
#define SQLITE_N_LIMIT (SQLITE_LIMIT_WORKER_THREADS+1)

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
	u8 bMalloced;		/* True if pStart obtained from sqlite3_malloc() */
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
 * A hash table for built-in function definitions.  (Application-defined
 * functions use a regular table table from hash.h.)
 *
 * Hash each FuncDef structure into one of the FuncDefHash.a[] slots.
 * Collisions are on the FuncDef.u.pHash chain.
 */
#define SQLITE_FUNC_HASH_SZ 23
struct FuncDefHash {
	FuncDef *a[SQLITE_FUNC_HASH_SZ];	/* Hash table for functions */
};

/*
 * Each database connection is an instance of the following structure.
 */
struct sqlite3 {
	sqlite3_vfs *pVfs;	/* OS Interface */
	struct Vdbe *pVdbe;	/* List of active virtual machines */
	struct coll *pDfltColl;	/* The default collating sequence (BINARY) */
	struct Schema *pSchema; /* Schema of the database */
	i64 szMmap;		/* Default mmap_size setting */
	int errCode;		/* Most recent error code (SQLITE_*) */
	int errMask;		/* & result codes with this before returning */
	int iSysErrno;		/* Errno value from last system error */
	u16 dbOptFlags;		/* Flags to enable/disable optimizations */
	u8 enc;			/* Text encoding */
	u8 temp_store;		/* 1: file 2: memory 0: default */
	u8 mallocFailed;	/* True if we have seen a malloc failure */
	u8 bBenignMalloc;	/* Do not require OOMs if true */
	u8 dfltLockMode;	/* Default locking-mode for attached dbs */
	u8 suppressErr;		/* Do not issue error messages if true */
	u8 mTrace;		/* zero or more SQLITE_TRACE flags */
	u32 magic;		/* Magic number for detect library misuse */
	int nChange;		/* Value returned by sqlite3_changes() */
	int nTotalChange;	/* Value returned by sqlite3_total_changes() */
	int aLimit[SQLITE_N_LIMIT];	/* Limits */
	int nMaxSorterMmap;	/* Maximum size of regions mapped by sorter */
	struct sqlite3InitInfo {	/* Information used during initialization */
		int newTnum;	/* Rootpage of table being initialized */
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
				 sqlite_int64);
#ifdef SQLITE_ENABLE_PREUPDATE_HOOK
	void *pPreUpdateArg;	/* First argument to xPreUpdateCallback */
	void (*xPreUpdateCallback) (	/* Registered using sqlite3_preupdate_hook() */
					   void *, sqlite3 *, int, char const *,
					   char const *, sqlite3_int64,
					   sqlite3_int64);
	PreUpdate *pPreUpdate;	/* Context for active pre-update callback */
#endif				/* SQLITE_ENABLE_PREUPDATE_HOOK */
	sqlite3_value *pErr;	/* Most recent error message */
	union {
		volatile int isInterrupted;	/* True if sqlite3_interrupt has been called */
		double notUsed1;	/* Spacer */
	} u1;
	Lookaside lookaside;	/* Lookaside malloc configuration */
#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
	int (*xProgress) (void *);	/* The progress callback */
	void *pProgressArg;	/* Argument to the progress callback */
	unsigned nProgressOps;	/* Number of opcodes for progress callback */
#endif
	Hash aFunc;		/* Hash table of connection functions */
	BusyHandler busyHandler;	/* Busy callback */
	int busyTimeout;	/* Busy handler timeout, in msec */
	int *pnBytesFreed;	/* If not NULL, increment this in DbFree() */
};

/*
 * Possible values for the sqlite3.flags.
 */
#define SQLITE_VdbeTrace      0x00000001	/* True to trace VDBE execution */
#define SQLITE_InternChanges  0x00000002	/* Uncommitted Hash table changes */
#define SQLITE_FullColNames   0x00000004	/* Show full column names on SELECT */
#define SQLITE_ShortColNames  0x00000040	/* Show short columns names */
#define SQLITE_CountRows      0x00000080	/* Count rows changed by INSERT, */
					  /*   DELETE, or UPDATE and return */
					  /*   the count using a callback. */
#define SQLITE_NullCallback   0x00000100	/* Invoke the callback once if the */
					  /*   result set is empty */
#define SQLITE_SqlTrace       0x00000200	/* Debug print SQL as it executes */
#define SQLITE_SelectTrace    0x00000800       /* Debug info about select statement */
#define SQLITE_WhereTrace     0x00008000       /* Debug info about optimizer's work */
#define SQLITE_VdbeListing    0x00000400	/* Debug listings of VDBE programs */
#define SQLITE_VdbeAddopTrace 0x00001000	/* Trace sqlite3VdbeAddOp() calls */
#define SQLITE_IgnoreChecks   0x00002000	/* Do not enforce check constraints */
#define SQLITE_ReadUncommitted 0x0004000	/* For shared-cache mode */
#define SQLITE_ReverseOrder   0x00020000	/* Reverse unordered SELECTs */
#define SQLITE_RecTriggers    0x00040000	/* Enable recursive triggers */
#define SQLITE_ForeignKeys    0x00080000	/* Enforce foreign key constraints  */
#define SQLITE_AutoIndex      0x00100000	/* Enable automatic indexes */
#define SQLITE_PreferBuiltin  0x00200000	/* Preference to built-in funcs */
#define SQLITE_EnableTrigger  0x01000000	/* True to enable triggers */
#define SQLITE_DeferFKs       0x02000000	/* Defer all FK constraints */
#define SQLITE_QueryOnly      0x04000000	/* Disable database changes */
#define SQLITE_VdbeEQP        0x08000000	/* Debug EXPLAIN QUERY PLAN */
#define SQLITE_NoCkptOnClose  0x80000000	/* No checkpoint on close()/DETACH */

/*
 * Bits of the sqlite3.dbOptFlags field that are used by the
 * sqlite3_test_control(SQLITE_TESTCTRL_OPTIMIZATIONS,...) interface to
 * selectively disable various optimizations.
 */
#define SQLITE_QueryFlattener 0x0001	/* Query flattening */
#define SQLITE_ColumnCache    0x0002	/* Column cache */
#define SQLITE_GroupByOrder   0x0004	/* GROUPBY cover of ORDERBY */
#define SQLITE_FactorOutConst 0x0008	/* Constant factoring */
/*                not used    0x0010   // Was: SQLITE_IdxRealAsInt */
#define SQLITE_DistinctOpt    0x0020	/* DISTINCT using indexes */
#define SQLITE_CoverIdxScan   0x0040	/* Covering index scans */
#define SQLITE_OrderByIdxJoin 0x0080	/* ORDER BY of joins via index */
#define SQLITE_SubqCoroutine  0x0100	/* Evaluate subqueries as coroutines */
#define SQLITE_Transitive     0x0200	/* Transitive constraints */
#define SQLITE_OmitNoopJoin   0x0400	/* Omit unused tables in joins */
#define SQLITE_CursorHints    0x2000	/* Add OP_CursorHint opcodes */
#define SQLITE_AllOpts        0xffff	/* All optimizations */

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
 * Possible values for the sqlite.magic field.
 * The numbers are obtained at random and have no special meaning, other
 * than being distinct from one another.
 */
#define SQLITE_MAGIC_OPEN     0xa029a697	/* Database is open */
#define SQLITE_MAGIC_CLOSED   0x9f3c2d33	/* Database is closed */
#define SQLITE_MAGIC_SICK     0x4b771290	/* Error and awaiting close */
#define SQLITE_MAGIC_BUSY     0xf03b7906	/* Database currently in use */
#define SQLITE_MAGIC_ERROR    0xb5357930	/* An SQLITE_MISUSE error occurred */
#define SQLITE_MAGIC_ZOMBIE   0x64cffc7f	/* Close with last statement close */

/*
 * Each SQL function is defined by an instance of the following
 * structure.  For global built-in functions (ex: substr(), max(), count())
 * a pointer to this structure is held in the sqlite3BuiltinFunctions object.
 * For per-connection application-defined functions, a pointer to this
 * structure is held in the db->aHash hash table.
 *
 * The u.pHash field is used by the global built-ins.  The u.pDestructor
 * field is used by per-connection app-def functions.
 */
struct FuncDef {
	i8 nArg;		/* Number of arguments.  -1 means unlimited */
	u16 funcFlags;		/* Some combination of SQLITE_FUNC_* */
	void *pUserData;	/* User data parameter */
	FuncDef *pNext;		/* Next function with same name */
	void (*xSFunc) (sqlite3_context *, int, sqlite3_value **);	/* func or agg-step */
	void (*xFinalize) (sqlite3_context *);	/* Agg finalizer */
	const char *zName;	/* SQL name of the function. */
	union {
		FuncDef *pHash;	/* Next with a different name but the same hash */
		FuncDestructor *pDestructor;	/* Reference counted destructor function */
	} u;
};

/*
 * This structure encapsulates a user-function destructor callback (as
 * configured using create_function_v2()) and a reference counter. When
 * create_function_v2() is called to create a function with a destructor,
 * a single object of this type is allocated. FuncDestructor.nRef is set to
 * the number of FuncDef objects created (either 1 or 3, depending on whether
 * or not the specified encoding is SQLITE_ANY). The FuncDef.pDestructor
 * member of each of the new FuncDef objects is set to point to the allocated
 * FuncDestructor.
 *
 * Thereafter, when one of the FuncDef objects is deleted, the reference
 * count on this object is decremented. When it reaches 0, the destructor
 * is invoked and the FuncDestructor structure freed.
 */
struct FuncDestructor {
	int nRef;
	void (*xDestroy) (void *);
	void *pUserData;
};

/*
 * Possible values for FuncDef.flags.  Note that the _LENGTH and _TYPEOF
 * values must correspond to OPFLAG_LENGTHARG and OPFLAG_TYPEOFARG.  And
 * SQLITE_FUNC_CONSTANT must be the same as SQLITE_DETERMINISTIC.  There
 * are assert() statements in the code to verify this.
 *
 * Value constraints (enforced via assert()):
 *     SQLITE_FUNC_MINMAX    ==  NC_MinMaxAgg      == SF_MinMaxAgg
 *     SQLITE_FUNC_LENGTH    ==  OPFLAG_LENGTHARG
 *     SQLITE_FUNC_TYPEOF    ==  OPFLAG_TYPEOFARG
 *     SQLITE_FUNC_CONSTANT  ==  SQLITE_DETERMINISTIC from the API
 */
#define SQLITE_FUNC_LIKE     0x0004	/* Candidate for the LIKE optimization */
#define SQLITE_FUNC_CASE     0x0008	/* Case-sensitive LIKE-type function */
#define SQLITE_FUNC_EPHEM    0x0010	/* Ephemeral.  Delete with VDBE */
#define SQLITE_FUNC_NEEDCOLL 0x0020	/* sqlite3GetFuncCollSeq() might be called */
#define SQLITE_FUNC_LENGTH   0x0040	/* Built-in length() function */
#define SQLITE_FUNC_TYPEOF   0x0080	/* Built-in typeof() function */
#define SQLITE_FUNC_COUNT    0x0100	/* Built-in count(*) aggregate */
#define SQLITE_FUNC_COALESCE 0x0200	/* Built-in coalesce() or ifnull() */
#define SQLITE_FUNC_UNLIKELY 0x0400	/* Built-in unlikely() function */
#define SQLITE_FUNC_CONSTANT 0x0800	/* Constant inputs give a constant output */
#define SQLITE_FUNC_MINMAX   0x1000	/* True for min() and max() aggregates */
#define SQLITE_FUNC_SLOCHNG  0x2000	/* "Slow Change". Value constant during a
					 * single query - might change over time
					 */

/*
 * The following three macros, FUNCTION(), LIKEFUNC() and AGGREGATE() are
 * used to create the initializers for the FuncDef structures.
 *
 *   FUNCTION(zName, nArg, iArg, bNC, xFunc)
 *     Used to create a scalar function definition of a function zName
 *     implemented by C function xFunc that accepts nArg arguments. The
 *     value passed as iArg is cast to a (void*) and made available
 *     as the user-data (sqlite3_user_data()) for the function. If
 *     argument bNC is true, then the SQLITE_FUNC_NEEDCOLL flag is set.
 *
 *   VFUNCTION(zName, nArg, iArg, bNC, xFunc)
 *     Like FUNCTION except it omits the SQLITE_FUNC_CONSTANT flag.
 *
 *   DFUNCTION(zName, nArg, iArg, bNC, xFunc)
 *     Like FUNCTION except it omits the SQLITE_FUNC_CONSTANT flag and
 *     adds the SQLITE_FUNC_SLOCHNG flag.  Used for date & time functions
 *     and functions like sqlite_version() that can change, but not during
 *     a single query.
 *
 *   AGGREGATE(zName, nArg, iArg, bNC, xStep, xFinal)
 *     Used to create an aggregate function definition implemented by
 *     the C functions xStep and xFinal. The first four parameters
 *     are interpreted in the same way as the first 4 parameters to
 *     FUNCTION().
 *
 *   LIKEFUNC(zName, nArg, pArg, flags)
 *     Used to create a scalar function definition of a function zName
 *     that accepts nArg arguments and is implemented by a call to C
 *     function likeFunc. Argument pArg is cast to a (void *) and made
 *     available as the function user-data (sqlite3_user_data()). The
 *     FuncDef.flags variable is set to the value passed as the flags
 *     parameter.
 */
#define FUNCTION(zName, nArg, iArg, bNC, xFunc) \
  {nArg, SQLITE_FUNC_CONSTANT|(bNC*SQLITE_FUNC_NEEDCOLL), \
   SQLITE_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0} }
#define VFUNCTION(zName, nArg, iArg, bNC, xFunc) \
  {nArg, (bNC*SQLITE_FUNC_NEEDCOLL), \
   SQLITE_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0} }
#define DFUNCTION(zName, nArg, iArg, bNC, xFunc) \
  {nArg, SQLITE_FUNC_SLOCHNG|(bNC*SQLITE_FUNC_NEEDCOLL), \
   SQLITE_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0} }
#define FUNCTION2(zName, nArg, iArg, bNC, xFunc, extraFlags) \
  {nArg,SQLITE_FUNC_CONSTANT|(bNC*SQLITE_FUNC_NEEDCOLL)|extraFlags,\
   SQLITE_INT_TO_PTR(iArg), 0, xFunc, 0, #zName, {0} }
#define STR_FUNCTION(zName, nArg, pArg, bNC, xFunc) \
  {nArg, SQLITE_FUNC_SLOCHNG|(bNC*SQLITE_FUNC_NEEDCOLL), \
   pArg, 0, xFunc, 0, #zName, }
#define LIKEFUNC(zName, nArg, arg, flags) \
  {nArg, SQLITE_FUNC_CONSTANT|flags, \
   (void *)arg, 0, likeFunc, 0, #zName, {0} }
#define AGGREGATE(zName, nArg, arg, nc, xStep, xFinal) \
  {nArg, (nc*SQLITE_FUNC_NEEDCOLL), \
   SQLITE_INT_TO_PTR(arg), 0, xStep,xFinal,#zName, {0}}
#define AGGREGATE2(zName, nArg, arg, nc, xStep, xFinal, extraFlags) \
  {nArg, (nc*SQLITE_FUNC_NEEDCOLL)|extraFlags, \
   SQLITE_INT_TO_PTR(arg), 0, xStep,xFinal,#zName, {0}}

/*
 * All current savepoints are stored in a linked list starting at
 * sqlite3.pSavepoint. The first element in the list is the most recently
 * opened savepoint. Savepoints are added to the list by the vdbe
 * OP_Savepoint instruction.
 */
struct Savepoint {
	box_txn_savepoint_t *tnt_savepoint; /* Tarantool's savepoint struct */
	char *zName;		/* Savepoint name (nul-terminated) */
	Savepoint *pNext;	/* Parent savepoint (if any) */
};

/*
 * The following are used as the second parameter to sqlite3Savepoint(),
 * and as the P1 argument to the OP_Savepoint instruction.
 */
#define SAVEPOINT_BEGIN      0
#define SAVEPOINT_RELEASE    1
#define SAVEPOINT_ROLLBACK   2

/*
 * information about each column of an SQL table is held in an instance
 * of this structure.
 */
struct Column {
	/** Collating sequence. */
	struct coll *coll;
	u8 is_primkey;		/* Boolean propertie for being PK */
};

#define sqlite3IsNumericAffinity(X)  ((X)>=AFFINITY_NUMERIC)

/*
 * The AFFINITY_MASK values masks off the significant bits of an
 * affinity value.
 */
#define AFFINITY_MASK     0x47

/*
 * Additional bit values that can be ORed with an affinity without
 * changing the affinity.
 *
 * The SQLITE_NOTNULL flag is a combination of NULLEQ and JUMPIFNULL.
 * It causes an assert() to fire if either operand to a comparison
 * operator is NULL.  It is added to certain comparison operators to
 * prove that the operands are always NOT NULL.
 */
#define SQLITE_KEEPNULL     0x08	/* Used by vector == or <> */
#define SQLITE_JUMPIFNULL   0x10	/* jumps if either operand is NULL */
#define SQLITE_STOREP2      0x20	/* Store result in reg[P2] rather than jump */
#define SQLITE_NULLEQ       0x80	/* NULL=NULL */
#define SQLITE_NOTNULL      0x90	/* Assert that operands are never NULL */

/*
 * The schema for each SQL table and view is represented in memory
 * by an instance of the following structure.
 */
struct Table {
	Column *aCol;		/* Information about each column */
	Index *pIndex;		/* List of SQL indexes on this table. */
	Select *pSelect;	/* NULL for tables.  Points to definition if a view. */
	FKey *pFKey;		/* Linked list of all foreign keys in this table */
	char *zColAff;		/* String defining the affinity of each column */
	ExprList *pCheck;	/* All CHECK constraints */
	/*   ... also used as column name list in a VIEW */
	Hash idxHash;		/* All (named) indices indexed by name */
	int tnum;		/* Root BTree page for this table */
	u32 nTabRef;		/* Number of pointers to this Table */
	i16 iPKey;		/* If not negative, use aCol[iPKey] as the rowid */
	i16 iAutoIncPKey;	/* If PK is marked INTEGER PRIMARY KEY AUTOINCREMENT, store
				   column number here, -1 otherwise Tarantool specifics */
	/**
	 * Estimated number of entries in table.
	 * Used only when table represents temporary objects,
	 * such as nested SELECTs or VIEWs. Otherwise, this stat
	 * can be fetched from space struct.
	 */
	LogEst tuple_log_count;
	u8 tabFlags;		/* Mask of TF_* values */
	u8 keyConf;		/* What to do in case of uniqueness conflict on iPKey */
#ifndef SQLITE_OMIT_ALTERTABLE
	int addColOffset;	/* Offset in CREATE TABLE stmt to add a new column */
#endif
	Trigger *pTrigger;	/* List of triggers stored in pSchema */
	Schema *pSchema;	/* Schema that contains this table */
	Table *pNextZombie;	/* Next on the Parse.pZombieTab list */
	/** Space definition with Tarantool metadata. */
	struct space_def *def;
};

/**
 * Return logarithm of tuple count in space.
 *
 * @param tab Table containing id of space to be examined.
 * @retval Logarithm of tuple count in space, or default values,
 *         if there is no corresponding space for given table.
 */
LogEst
sql_space_tuple_log_count(struct Table *tab);

/*
 * Allowed values for Table.tabFlags.
 */
#define TF_Ephemeral       0x02	/* An ephemeral table */
#define TF_HasPrimaryKey   0x04	/* Table has a primary key */
#define TF_Autoincrement   0x08	/* Integer primary key is autoincrement */

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
 *
 * The list of all parents for child Table X is held at X.pFKey.
 *
 * A list of all children for a table named Z (which might not even exist)
 * is held in Schema.fkeyHash with a hash key of Z.
 */
struct FKey {
	Table *pFrom;		/* Table containing the REFERENCES clause (aka: Child) */
	FKey *pNextFrom;	/* Next FKey with the same in pFrom. Next parent of pFrom */
	char *zTo;		/* Name of table that the key points to (aka: Parent) */
	FKey *pNextTo;		/* Next with the same zTo. Next child of zTo. */
	FKey *pPrevTo;		/* Previous with the same zTo */
	int nCol;		/* Number of columns in this key */
	/* EV: R-30323-21917 */
	u8 isDeferred;		/* True if constraint checking is deferred till COMMIT */
	u8 aAction[2];		/* ON DELETE and ON UPDATE actions, respectively */
	Trigger *apTrigger[2];	/* Triggers for aAction[] actions */
	struct sColMap {	/* Mapping of columns in pFrom to columns in zTo */
		int iFrom;	/* Index of column in pFrom */
		char *zCol;	/* Name of column in zTo.  If NULL use PRIMARY KEY */
	} aCol[1];		/* One entry for each of nCol columns */
};

/*
 * RESTRICT, SETNULL, and CASCADE actions apply only to foreign keys.
 * RESTRICT is the same as ABORT for IMMEDIATE foreign keys and the
 * same as ROLLBACK for DEFERRED keys.  SETNULL means that the foreign
 * key is set to NULL.  CASCADE means that a DELETE or UPDATE of the
 * referenced table row is propagated into the row that holds the
 * foreign key.
 */
#define OE_Restrict 6		/* OE_Abort for IMMEDIATE, OE_Rollback for DEFERRED */
#define OE_SetNull  7		/* Set the foreign key value to NULL */
#define OE_SetDflt  8		/* Set the foreign key value to its default */
#define OE_Cascade  9		/* Cascade the changes */

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
	u8 errCode;		/* Error detected by xRecordCompare (CORRUPT or NOMEM) */
	i8 r1;			/* Value to return if (lhs > rhs) */
	i8 r2;			/* Value to return if (rhs < lhs) */
	u8 eqSeen;		/* True if an equality comparison has been seen */
	u8 opcode;		/* Currently executing opcode that invoked
				 * movetoUnpacked, used by Tarantool storage layer.
				 */
};

/*
 * Each SQL index is represented in memory by an
 * instance of the following structure.
 *
 * The columns of the table that are to be indexed are described
 * by the aiColumn[] field of this structure.  For example, suppose
 * we have the following table and index:
 *
 *     CREATE TABLE Ex1(c1 int, c2 int, c3 text);
 *     CREATE INDEX Ex2 ON Ex1(c3,c1);
 *
 * In the Table structure describing Ex1, nCol==3 because there are
 * three columns in the table.  In the Index structure describing
 * Ex2, nColumn==2 since 2 of the 3 columns of Ex1 are indexed.
 * The value of aiColumn is {2, 0}.  aiColumn[0]==2 because the
 * first column to be indexed (c3) has an index of 2 in Ex1.aCol[].
 * The second column to be indexed (c1) has an index of 0 in
 * Ex1.aCol[], hence Ex2.aiColumn[1]==0.
 *
 * The Index.onError field determines whether or not the indexed columns
 * must be unique and what to do if they are not.  When Index.onError=
 * ON_CONFLICT_ACTION_NONE, it means this is not a unique index.
 * Otherwise it is a unique index and the value of Index.onError indicate
 * the which conflict resolution algorithm to employ whenever an attempt
 * is made to insert a non-unique element.
 *
 * While parsing a CREATE TABLE or CREATE INDEX statement in order to
 * generate VDBE code (as opposed to reading from Tarantool's _space
 * space as part of parsing an existing database schema), transient instances
 * of this structure may be created. In this case the Index.tnum variable is
 * used to store the address of a VDBE instruction, not a database page
 * number (it cannot - the database page is not allocated until the VDBE
 * program is executed). See convertToWithoutRowidTable() for details.
 */
struct Index {
	char *zName;		/* Name of this index */
	i16 *aiColumn;		/* Which columns are used by this index.  1st is 0 */
	LogEst *aiRowLogEst;	/* From ANALYZE: Est. rows selected by each column */
	Table *pTable;		/* The SQL table being indexed */
	char *zColAff;		/* String defining the affinity of each column */
	Index *pNext;		/* The next index associated with the same table */
	Schema *pSchema;	/* Schema containing this index */
	/** Sorting order for each column. */
	enum sort_order *sort_order;
	/** Array of collation sequences for index. */
	struct coll **coll_array;
	/** Array of collation identifiers. */
	uint32_t *coll_id_array;
	Expr *pPartIdxWhere;	/* WHERE clause for partial indices */
	ExprList *aColExpr;	/* Column expressions */
	int tnum;		/* DB Page containing root of this index */
	u16 nColumn;		/* Number of columns stored in the index */
	u8 onError;		/* ON_CONFLICT_ACTION_ABORT, _IGNORE, _REPLACE,
				 * or _NONE
				 */
	unsigned idxType:2;	/* 1==UNIQUE, 2==PRIMARY KEY, 0==CREATE INDEX */
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
 * @param idx Index.
 * @param field Number of field to be examined.
 * @retval Estimate logarithm of tuples selected by given field.
 */
log_est_t
index_field_tuple_est(struct Index *idx, uint32_t field);

/*
 * Allowed values for Index.idxType
 */
#define SQLITE_IDXTYPE_APPDEF      0	/* Created using CREATE INDEX */
#define SQLITE_IDXTYPE_UNIQUE      1	/* Implements a UNIQUE constraint */
#define SQLITE_IDXTYPE_PRIMARYKEY  2	/* Is the PRIMARY KEY for the table */

/* Return true if index X is a PRIMARY KEY index */
#define IsPrimaryKeyIndex(X)  ((X)->idxType==SQLITE_IDXTYPE_PRIMARYKEY)

/* Return true if index X is a UNIQUE index */
#define IsUniqueIndex(X)      (((X)->idxType == SQLITE_IDXTYPE_UNIQUE) || \
				((X)->idxType == SQLITE_IDXTYPE_PRIMARYKEY))

/* The Index.aiColumn[] values are normally positive integer.  But
 * there are some negative values that have special meaning:
 */
#define XN_EXPR      (-2)	/* Indexed column is an expression */

#ifdef DEFAULT_TUPLE_COUNT
#undef DEFAULT_TUPLE_COUNT
#endif
#define DEFAULT_TUPLE_COUNT 1048576
/** [10*log_{2}(1048576)] == 200 */
#define DEFAULT_TUPLE_LOG_COUNT 200

/*
 * Each token coming out of the lexer is an instance of
 * this structure.  Tokens are also used as part of an expression.
 *
 * Note if Token.z==0 then Token.dyn and Token.n are undefined and
 * may contain random values.  Do not make any assumptions about Token.dyn
 * and Token.n when Token.z==0.
 */
struct Token {
	const char *z;		/* Text of the token.  Not NULL-terminated! */
	unsigned int n;		/* Number of characters in this token */
	bool isReserved;         /* If reserved keyword or not */
};

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
		FuncDef *pFunc;	/* The aggregate function implementation */
		int iMem;	/* Memory location that acts as accumulator */
		int iDistinct;	/* Ephemeral table used to enforce DISTINCT */
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
 * For such expressions, Expr.op is set to TK_COLUMN and Expr.iTable is
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
 * is a disk table or the "old.*" pseudo-table, then pTab points to the
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
	char affinity;		/* The affinity of the column or 0 if not a column */
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

#if SQLITE_MAX_EXPR_DEPTH>0
	int nHeight;		/* Height of the tree headed by this node */
#endif
	int iTable;		/* TK_COLUMN: cursor number of table holding column
				 * TK_REGISTER: register number
				 * TK_TRIGGER: 1 -> new, 0 -> old
				 * EP_Unlikely:  134217728 times likelihood
				 * TK_SELECT: 1st register of result vector
				 */
	bool is_ephemeral;      /* If iTable was set, this flags if this table i
				 * ephemeral or not.
				 */
	ynVar iColumn;		/* TK_COLUMN: column index.
				 * TK_VARIABLE: variable number (always >= 1).
				 * TK_SELECT_COLUMN: column of the result vector
				 */
	i16 iAgg;		/* Which entry in pAggInfo->aCol[] or ->aFunc[] */
	i16 iRightJoinTable;	/* If EP_FromJoin, the right table of the join */
	u8 op2;			/* TK_REGISTER: original value of Expr.op
				 * TK_COLUMN: the value of p5 for OP_Column
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
#define EP_InfixFunc 0x000080	/* True for an infix function: LIKE, GLOB, etc */
#define EP_Collate   0x000100	/* Tree contains a TK_COLLATE operator */
#define EP_Generic   0x000200	/* Ignore COLLATE or affinity on this tree */
#define EP_IntValue  0x000400	/* Integer value contained in u.iValue */
#define EP_xIsSelect 0x000800	/* x.pSelect is valid (otherwise x.pList is) */
#define EP_Skip      0x001000	/* COLLATE, AS, or UNLIKELY */
#define EP_Reduced   0x002000	/* Expr struct EXPR_REDUCEDSIZE bytes only */
#define EP_TokenOnly 0x004000	/* Expr struct EXPR_TOKENONLYSIZE bytes only */
#define EP_Static    0x008000	/* Held in memory not obtained from malloc() */
#define EP_MemToken  0x010000	/* Need to sqlite3DbFree() Expr.zToken */
#define EP_NoReduce  0x020000	/* Cannot EXPRDUP_REDUCE this Expr */
#define EP_Unlikely  0x040000	/* unlikely() or likelihood() function */
#define EP_ConstFunc 0x080000	/* A SQLITE_FUNC_CONSTANT or _SLOCHNG function */
#define EP_CanBeNull 0x100000	/* Can be null despite NOT NULL constraint */
#define EP_Subquery  0x200000	/* Tree contains a TK_SELECT operator */
#define EP_Alias     0x400000	/* Is an alias for a result set column */
#define EP_Leaf      0x800000	/* Expr.pLeft, .pRight, .u.pSelect all NULL */

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
#ifdef SQLITE_DEBUG
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
 * Flags passed to the sqlite3ExprDup() function. See the header comment
 * above sqlite3ExprDup() for details.
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
#ifdef SQLITE_BITMASK_TYPE
typedef SQLITE_BITMASK_TYPE Bitmask;
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
 * such a table must be a simple name: ID.  But in SQLite, the table can
 * now be identified by a database name, a dot, then the table name: ID.ID.
 *
 * The jointype starts out showing the join type between the current table
 * and the next table on the list.  The parser builds the list this way.
 * But sqlite3SrcListShiftJoinType() later shifts the jointypes so that each
 * jointype expresses the join between the table and the previous table.
 *
 * In the colUsed field, the high-order bit (bit 63) is set if the table
 * contains more than 63 columns and the 64-th or later column is used.
 */
struct SrcList {
	int nSrc;		/* Number of tables or subqueries in the FROM clause */
	u32 nAlloc;		/* Number of entries allocated in a[] below */
	struct SrcList_item {
		Schema *pSchema;	/* Schema to which this item is fixed */
		char *zName;	/* Name of the table */
		char *zAlias;	/* The "B" part of a "A AS B" phrase.  zName is the "A" */
		Table *pTab;	/* An SQL table corresponding to zName */
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
		Bitmask colUsed;	/* Bit N (1<<N) set if column N of pTab is used */
		union {
			char *zIndexedBy;	/* Identifier from "INDEXED BY <zIndex>" clause */
			ExprList *pFuncArg;	/* Arguments to table-valued-function */
		} u1;
		Index *pIBIndex;	/* Index structure corresponding to u1.zIndexedBy */
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
 * Flags appropriate for the wctrlFlags parameter of sqlite3WhereBegin()
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
#define WHERE_SORTBYGROUP      0x0200	/* Support sqlite3WhereIsSorted() */
#define WHERE_SEEK_TABLE       0x0400	/* Do not defer seeks on main table */
#define WHERE_ORDERBY_LIMIT    0x0800	/* ORDERBY+LIMIT on the inner loop */
			/*     0x1000    not currently used */
			/*     0x2000    not currently used */
#define WHERE_USE_LIMIT        0x4000	/* Use the LIMIT in cost estimates */
			/*     0x8000    not currently used */

/* Allowed return values from sqlite3WhereIsDistinct()
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
 *    NC_MinMaxAgg == SF_MinMaxAgg == SQLITE_FUNC_MINMAX
 *
 */
#define NC_AllowAgg  0x0001	/* Aggregate functions are allowed here */
#define NC_PartIdx   0x0002	/* True if resolving a partial index WHERE */
#define NC_IsCheck   0x0004	/* True if resolving names in a CHECK constraint */
#define NC_InAggFunc 0x0008	/* True if analyzing arguments to an agg func */
#define NC_HasAgg    0x0010	/* One or more aggregate functions seen */
#define NC_IdxExpr   0x0020	/* True if resolving columns of CREATE INDEX */
#define NC_VarSelect 0x0040	/* A correlated subquery has been seen */
#define NC_MinMaxAgg 0x1000	/* min/max aggregates seen.  See note above */

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
 * the P4_KEYDEF and P2 parameters later.  Neither the key_def nor
 * the number of columns in P2 can be computed at the same time
 * as the OP_OpenEphm instruction is coded because not
 * enough information about the compound query is known at that point.
 * The key_def for addrOpenTran[0] and [1] contains collating sequences
 * for the result set.  The key_def for addrOpenEphm[2] contains collating
 * sequences for the ORDER BY clause.
 */
struct Select {
	ExprList *pEList;	/* The fields of the result */
	u8 op;			/* One of: TK_UNION TK_ALL TK_INTERSECT TK_EXCEPT */
	LogEst nSelectRow;	/* Estimated number of result rows */
	u32 selFlags;		/* Various SF_* values */
	int iLimit, iOffset;	/* Memory registers holding LIMIT & OFFSET counters */
#ifdef SELECTTRACE_ENABLED
	char zSelName[12];	/* Symbolic name of this SELECT use for debugging */
#endif
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
 *     SF_MinMaxAgg  == NC_MinMaxAgg     == SQLITE_FUNC_MINMAX
 *     SF_FixedLimit == WHERE_USE_LIMIT
 */
#define SF_Distinct       0x00001	/* Output should be DISTINCT */
#define SF_All            0x00002	/* Includes the ALL keyword */
#define SF_Resolved       0x00004	/* Identifiers have been resolved */
#define SF_Aggregate      0x00008	/* Contains agg functions or a GROUP BY */
#define SF_HasAgg         0x00010	/* Contains aggregate functions */
#define SF_UsesEphemeral  0x00020	/* Uses the OpenEphemeral opcode */
#define SF_Expanded       0x00040	/* sqlite3SelectExpand() called on this */
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
 *                     Apply the affinity pDest->affSdst before storing
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
	char *zAffSdst;		/* Affinity used when eDest==SRT_Set */
	int iSDParm;		/* A parameter used by the eDest disposal method */
	int iSdst;		/* Base register where results are written */
	int nSdst;		/* Number of registers allocated */
	ExprList *pOrderBy;	/* Key columns for SRT_Queue and SRT_DistQueue */
};

/*
 * Size of the column cache
 */
#ifndef SQLITE_N_COLCACHE
#define SQLITE_N_COLCACHE 10
#endif

/*
 * At least one instance of the following structure is created for each
 * trigger that may be fired while parsing an INSERT, UPDATE or DELETE
 * statement. All such objects are stored in the linked list headed at
 * Parse.pTriggerPrg and deleted once statement compilation has been
 * completed.
 *
 * A Vdbe sub-program that implements the body and WHEN clause of trigger
 * TriggerPrg.pTrigger, assuming a default ON CONFLICT clause of
 * TriggerPrg.orconf, is stored in the TriggerPrg.pProgram variable.
 * The Parse.pTriggerPrg list never contains two entries with the same
 * values for both pTrigger and orconf.
 *
 * The TriggerPrg.aColmask[0] variable is set to a mask of old.* columns
 * accessed (or set to 0 for triggers fired as a result of INSERT
 * statements). Similarly, the TriggerPrg.aColmask[1] variable is set to
 * a mask of new.* columns used by the program.
 */
struct TriggerPrg {
	Trigger *pTrigger;	/* Trigger this program was coded from */
	TriggerPrg *pNext;	/* Next entry in Parse.pTriggerPrg list */
	SubProgram *pProgram;	/* Program implementing pTrigger/orconf */
	int orconf;		/* Default ON CONFLICT policy */
	u32 aColmask[2];	/* Masks of old.*, new.* columns accessed */
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
	sqlite3 *db;		/* The main database structure */
	char *zErrMsg;		/* An error message */
	Vdbe *pVdbe;		/* An engine for executing database bytecode */
	int rc;			/* Return code from execution */
	u8 colNamesSet;		/* TRUE after OP_ColumnName has been issued to pVdbe */
	u8 checkSchema;		/* Causes schema cookie check after an error */
	u8 nested;		/* Number of nested calls to the parser/code generator */
	u8 nTempReg;		/* Number of temporary registers in aTempReg[] */
	u8 isMultiWrite;	/* True if statement may modify/insert multiple rows */
	u8 mayAbort;		/* True if statement may throw an ABORT exception */
	u8 hasCompound;		/* Need to invoke convertCompoundSelectToSubquery() */
	u8 okConstFactor;	/* OK to factor out constants */
	u8 disableLookaside;	/* Number of times lookaside has been disabled */
	u8 nColCache;		/* Number of entries in aColCache[] */
	int nRangeReg;		/* Size of the temporary register block */
	int iRangeReg;		/* First register in temporary register block */
	int nErr;		/* Number of errors seen */
	int nTab;		/* Number of previously allocated VDBE cursors */
	int nMem;		/* Number of memory cells used so far */
	int nOpAlloc;		/* Number of slots allocated for Vdbe.aOp[] */
	int szOpAlloc;		/* Bytes of memory space allocated for Vdbe.aOp[] */
	int ckBase;		/* Base register of data during check constraints */
	int iSelfTab;		/* Table of an index whose exprs are being coded */
	int iCacheLevel;	/* ColCache valid when aColCache[].iLevel<=iCacheLevel */
	int iCacheCnt;		/* Counter used to generate aColCache[].lru values */
	int nLabel;		/* Number of labels used */
	int *aLabel;		/* Space to hold the labels */
	ExprList *pConstExpr;	/* Constant expressions */
	Token constraintName;	/* Name of the constraint currently being parsed */
	int regRoot;		/* Register holding root page number for new objects */
	int nMaxArg;		/* Max args passed to user function by sub-program */
	int nSelect;		/* Number of SELECT statements seen */
	int nSelectIndent;	/* How far to indent SELECTTRACE() output */
	Parse *pToplevel;	/* Parse structure for main program (or NULL) */
	Table *pTriggerTab;	/* Table triggers are being coded for */
	u32 nQueryLoop;		/* Est number of iterations of a query (10*log2(N)) */
	u32 oldmask;		/* Mask of old.* columns referenced */
	u32 newmask;		/* Mask of new.* columns referenced */
	u8 eTriggerOp;		/* TK_UPDATE, TK_INSERT or TK_DELETE */
	u8 eOrconf;		/* Default ON CONFLICT policy for trigger steps */
	u8 disableTriggers;	/* True to disable triggers */
	/** Region size at the Parser launch. */
	size_t region_initial_size;

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
	} aColCache[SQLITE_N_COLCACHE];	/* One for each column cache entry */
	int aTempReg[8];	/* Holding area for temporary registers */
	Token sNameToken;	/* Token with unqualified schema object name */

  /************************************************************************
  * Above is constant between recursions.  Below is reset before and after
  * each recursion.  The boundary between these two regions is determined
  * using offsetof(Parse,sLastToken) so the sLastToken field must be the
  * first field in the recursive region.
  ***********************************************************************/

	Token sLastToken;	/* The last token parsed */
	ynVar nVar;		/* Number of '?' variables seen in the SQL so far */
	u8 iPkSortOrder;	/* ASC or DESC for INTEGER PRIMARY KEY */
	u8 explain;		/* True if the EXPLAIN flag is found on the query */
	int nHeight;		/* Expression tree height of current sub-select */
	int iSelectId;		/* ID of current select for EXPLAIN output */
	int iNextSelectId;	/* Next available select ID for EXPLAIN output */
	VList *pVList;		/* Mapping between variable names and numbers */
	Vdbe *pReprepare;	/* VM being reprepared (sqlite3Reprepare()) */
	const char *zTail;	/* All SQL text past the last semicolon parsed */
	Table *pNewTable;	/* A table being constructed by CREATE TABLE */
	Trigger *pNewTrigger;	/* Trigger under construct by a CREATE TRIGGER */
	Table *pZombieTab;	/* List of Table objects to delete after code gen */
	TriggerPrg *pTriggerPrg;	/* Linked list of coded triggers */
	With *pWith;		/* Current WITH clause, or NULL */
	With *pWithToFree;	/* Free this WITH object at the end of the parse */

	bool initiateTTrans;	/* Initiate Tarantool transaction */
	/** If set - do not emit byte code at all, just parse.  */
	bool parse_only;
	/** If parse_only is set to true, store parsed expression. */
	struct Expr *parsed_expr;
};

/*
 * Sizes and pointers of various parts of the Parse object.
 */
#define PARSE_HDR_SZ offsetof(Parse,aColCache)	/* Recursive part w/o aColCache */
#define PARSE_RECURSE_SZ offsetof(Parse,sLastToken)	/* Recursive part */
#define PARSE_TAIL_SZ (sizeof(Parse)-PARSE_RECURSE_SZ)	/* Non-recursive part */
#define PARSE_TAIL(X) (((char*)(X))+PARSE_RECURSE_SZ)	/* Pointer to tail */

/*
 * Bitfield flags for P5 value in various opcodes.
 *
 * Value constraints (enforced via assert()):
 *    OPFLAG_LENGTHARG    == SQLITE_FUNC_LENGTH
 *    OPFLAG_TYPEOFARG    == SQLITE_FUNC_TYPEOF
 *    OPFLAG_BULKCSR      == BTREE_BULKLOAD
 *    OPFLAG_SEEKEQ       == BTREE_SEEK_EQ
 *    OPFLAG_FORDELETE    == BTREE_FORDELETE
 *    OPFLAG_SAVEPOSITION == BTREE_SAVEPOSITION
 *    OPFLAG_AUXDELETE    == BTREE_AUXDELETE
 */
#define OPFLAG_NCHANGE       0x01	/* OP_Insert: Set to update db->nChange */
				     /* Also used in P2 (not P5) of OP_Delete */
#define OPFLAG_EPHEM         0x01	/* OP_Column: Ephemeral output is ok */
#define OPFLAG_ISUPDATE      0x04	/* This OP_Insert is an sql UPDATE */
#define OPFLAG_OE_IGNORE    0x200	/* OP_IdxInsert: Ignore flag */
#define OPFLAG_OE_FAIL      0x400	/* OP_IdxInsert: Fail flag */
#ifdef SQLITE_ENABLE_PREUPDATE_HOOK
#define OPFLAG_ISNOOP        0x40	/* OP_Delete does pre-update-hook only */
#endif
#define OPFLAG_LENGTHARG     0x40	/* OP_Column only used for length() */
#define OPFLAG_TYPEOFARG     0x80	/* OP_Column only used for typeof() */
#define OPFLAG_BULKCSR       0x01	/* OP_Open** used to open bulk cursor */
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
#define OPFLAG_FRESH_PTR     0x20	/* OP_Open**: set if space pointer
					 * comes from OP_SIDtoPtr, i.e. it
					 * is fresh, even in case schema
					 * changes previously.
					 */

/*
 * Each trigger present in the database schema is stored as an instance of
 * struct Trigger.
 *
 * Pointers to instances of struct Trigger are stored in two ways.
 * 1. In the "trigHash" hash table (part of the sqlite3* that represents the
 *    database). This allows Trigger structures to be retrieved by name.
 * 2. All triggers associated with a single table form a linked list, using the
 *    pNext member of struct Trigger. A pointer to the first element of the
 *    linked list is stored as the "pTrigger" member of the associated
 *    struct Table.
 *
 * The "step_list" member points to the first element of a linked list
 * containing the SQL statements specified as the trigger program.
 */
struct Trigger {
	char *zName;		/* The name of the trigger                        */
	char *table;		/* The table or view to which the trigger applies */
	u8 op;			/* One of TK_DELETE, TK_UPDATE, TK_INSERT         */
	u8 tr_tm;		/* One of TRIGGER_BEFORE, TRIGGER_AFTER */
	Expr *pWhen;		/* The WHEN clause of the expression (may be NULL) */
	IdList *pColumns;	/* If this is an UPDATE OF <column-list> trigger,
				   the <column-list> is stored here */
	Schema *pSchema;	/* Schema containing the trigger */
	Schema *pTabSchema;	/* Schema containing the table */
	TriggerStep *step_list;	/* Link list of trigger program steps             */
	Trigger *pNext;		/* Next trigger associated with the table */
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
 * associated struct Trigger instance. The first element of the linked list is
 * the first step of the trigger-program.
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
 *              them to. See sqlite3Update() documentation of "pChanges"
 *              argument.
 *
 */
struct TriggerStep {
	u8 op;			/* One of TK_DELETE, TK_UPDATE, TK_INSERT, TK_SELECT */
	u8 orconf;		/* ON_CONFLICT_ACTION_ROLLBACK etc. */
	Trigger *pTrig;		/* The trigger that this step is a part of */
	Select *pSelect;	/* SELECT statement or RHS of INSERT INTO SELECT ... */
	char *zTarget;		/* Target table for DELETE, UPDATE, INSERT */
	Expr *pWhere;		/* The WHERE clause for DELETE or UPDATE steps */
	ExprList *pExprList;	/* SET clause for UPDATE. */
	IdList *pIdList;	/* Column names for INSERT */
	TriggerStep *pNext;	/* Next in the link-list */
	TriggerStep *pLast;	/* Last element in link-list. Valid for 1st elem only */
};

/*
 * The following structure contains information used by the sqliteFix...
 * routines as they walk the parse tree to make database references
 * explicit.
 */
typedef struct DbFixer DbFixer;
struct DbFixer {
	Parse *pParse;		/* The parsing context.  Error messages written here */
	Schema *pSchema;	/* Fix items to this schema */
	int bVarOnly;		/* Check for variable references only */
	const char *zType;	/* Type of the container - used for error messages */
	const Token *pName;	/* Name of the container - used for error messages */
};

/*
 * An objected used to accumulate the text of a string where we
 * do not necessarily know how big the string will be in the end.
 */
struct StrAccum {
	sqlite3 *db;		/* Optional database for lookaside.  Can be NULL */
	char *zBase;		/* A base allocation.  Not from malloc. */
	char *zText;		/* The string collected so far */
	u32 nChar;		/* Length of the string so far */
	u32 nAlloc;		/* Amount of space allocated in zText */
	u32 mxAlloc;		/* Maximum allowed allocation.  0 for no malloc usage */
	u8 accError;		/* STRACCUM_NOMEM or STRACCUM_TOOBIG */
	u8 printfFlags;		/* SQLITE_PRINTF flags below */
};
#define STRACCUM_NOMEM   1
#define STRACCUM_TOOBIG  2
#define SQLITE_PRINTF_INTERNAL 0x01	/* Internal-use-only converters allowed */
#define SQLITE_PRINTF_SQLFUNC  0x02	/* SQL function arguments to VXPrintf */
#define SQLITE_PRINTF_MALLOCED 0x04	/* True if xText is allocated space */

#define isMalloced(X)  (((X)->printfFlags & SQLITE_PRINTF_MALLOCED)!=0)

/*
 * A pointer to this structure is used to communicate information
 * from sqlite3Init and OP_ParseSchema into the sqlite3InitCallback.
 */
typedef struct {
	sqlite3 *db;		/* The database being initialized */
	char **pzErrMsg;	/* Error message stored here */
	int rc;			/* Result code stored here */
} InitData;

/*
 * Structure containing global configuration data for the SQLite library.
 *
 * This structure also contains some state information.
 */
struct Sqlite3Config {
	int bMemstat;		/* True to enable memory status */
	int bOpenUri;		/* True to interpret filenames as URIs */
	int bUseCis;		/* Use covering indices for full-scans */
	int mxStrlen;		/* Maximum string length */
	int neverCorrupt;	/* Database is always well-formed */
	int szLookaside;	/* Default lookaside buffer size */
	int nLookaside;		/* Default lookaside buffer count */
	int nStmtSpill;		/* Stmt-journal spill-to-disk threshold */
	void *pHeap;		/* Heap storage space */
	int nHeap;		/* Size of pHeap[] */
	int mnReq, mxReq;	/* Min and max heap requests sizes */
	sqlite3_int64 szMmap;	/* mmap() space per open file */
	sqlite3_int64 mxMmap;	/* Maximum value for szMmap */
	void *pScratch;		/* Scratch memory */
	int szScratch;		/* Size of each scratch buffer */
	int nScratch;		/* Number of scratch buffers */
	void *pPage;		/* Page cache memory */
	int szPage;		/* Size of each page in pPage[] */
	int nPage;		/* Number of pages in pPage[] */
	int mxParserStack;	/* maximum depth of the parser stack */
	int sharedCacheEnabled;	/* true if shared-cache mode enabled */
	u32 szPma;		/* Maximum Sorter PMA size */
	/* The above might be initialized to non-zero.  The following need to always
	 * initially be zero, however.
	 */
	int isInit;		/* True after initialization has finished */
	int inProgress;		/* True while initialization in progress */
	int isMallocInit;	/* True after malloc is initialized */
	void (*xLog) (void *, int, const char *);	/* Function for logging */
	void *pLogArg;		/* First argument to xLog() */
#ifdef SQLITE_ENABLE_SQLLOG
	void (*xSqllog) (void *, sqlite3 *, const char *, int);
	void *pSqllogArg;
#endif
#ifdef SQLITE_VDBE_COVERAGE
	/* The following callback (if not NULL) is invoked on every VDBE branch
	 * operation.  Set the callback using SQLITE_TESTCTRL_VDBE_COVERAGE.
	 */
	void (*xVdbeBranch) (void *, int iSrcLine, u8 eThis, u8 eMx);	/* Callback */
	void *pVdbeBranchArg;	/* 1st argument */
#endif
#ifndef SQLITE_UNTESTABLE
	int (*xTestCallback) (int);	/* Invoked by sqlite3FaultSim() */
#endif
	int bLocaltimeFault;	/* True to fail localtime() calls */
	int iOnceResetThreshold;	/* When to reset OP_Once counters */
};

/*
 * This macro is used inside of assert() statements to indicate that
 * the assert is only valid on a well-formed database.  Instead of:
 *
 *     assert( X );
 *
 * One writes:
 *
 *     assert( X || CORRUPT_DB );
 *
 * CORRUPT_DB is true during normal operation.  CORRUPT_DB does not indicate
 * that the database is definitely corrupt, only that it might be corrupt.
 * For most test cases, CORRUPT_DB is set to false using a special
 * sqlite3_test_control().  This enables assert() statements to prove
 * things that are always true for well-formed databases.
 */
#define CORRUPT_DB  (sqlite3Config.neverCorrupt==0)

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
		struct CCurHint *pCCurHint;	/* Used by codeCursorHint() */
		int *aiCol;	/* array of column indexes */
		struct IdxCover *pIdxCover;	/* Check for index coverage */
	} u;
};

/* Forward declarations */
int sqlite3WalkExpr(Walker *, Expr *);
int sqlite3WalkExprList(Walker *, ExprList *);
int sqlite3WalkSelect(Walker *, Select *);
int sqlite3WalkSelectExpr(Walker *, Select *);
int sqlite3WalkSelectFrom(Walker *, Select *);
int sqlite3ExprWalkNoop(Walker *, Expr *);

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

#ifdef SQLITE_DEBUG
/*
 * An instance of the TreeView object is used for printing the content of
 * data structures on sqlite3DebugPrintf() using a tree-like view.
 */
struct TreeView {
	int iLevel;		/* Which level of the tree we are on */
	u8 bLine[100];		/* Draw vertical in column i if bLine[i] is true */
};
#endif				/* SQLITE_DEBUG */

/*
 * Assuming zIn points to the first byte of a UTF-8 character,
 * advance zIn to point to the first byte of the next UTF-8 character.
 */
#define SQLITE_SKIP_UTF8(zIn) {                        \
  if( (*(zIn++))>=0xc0 ){                              \
    while( (*zIn & 0xc0)==0x80 ){ zIn++; }             \
  }                                                    \
}

/*
 * The SQLITE_*_BKPT macros are substitutes for the error codes with
 * the same name but without the _BKPT suffix.  These macros invoke
 * routines that report the line-number on which the error originated
 * using sqlite3_log().  The routines also provide a convenient place
 * to set a debugger breakpoint.
 */
int sqlite3CorruptError(int);
int sqlite3MisuseError(int);
int sqlite3CantopenError(int);
#define SQLITE_CORRUPT_BKPT sqlite3CorruptError(__LINE__)
#define SQLITE_MISUSE_BKPT sqlite3MisuseError(__LINE__)
#define SQLITE_CANTOPEN_BKPT sqlite3CantopenError(__LINE__)
#ifdef SQLITE_DEBUG
int sqlite3NomemError(int);
int sqlite3IoerrnomemError(int);
#define SQLITE_NOMEM_BKPT sqlite3NomemError(__LINE__)
#define SQLITE_IOERR_NOMEM_BKPT sqlite3IoerrnomemError(__LINE__)
#else
#define SQLITE_NOMEM_BKPT SQLITE_NOMEM
#define SQLITE_IOERR_NOMEM_BKPT SQLITE_IOERR_NOMEM
#endif

/*
 * FTS4 is really an extension for FTS3.  It is enabled using the
 * SQLITE_ENABLE_FTS3 macro.  But to avoid confusion we also call
 * the SQLITE_ENABLE_FTS4 macro to serve as an alias for SQLITE_ENABLE_FTS3.
 */
#if defined(SQLITE_ENABLE_FTS4) && !defined(SQLITE_ENABLE_FTS3)
#define SQLITE_ENABLE_FTS3 1
#endif

/*
 * The following macros mimic the standard library functions toupper(),
 * isspace(), isalnum(), isdigit() and isxdigit(), respectively. The
 * sqlite versions only work for ASCII characters, regardless of locale.
 */
#define sqlite3Toupper(x)  ((x)&~(sqlite3CtypeMap[(unsigned char)(x)]&0x20))
#define sqlite3Isspace(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x01)
#define sqlite3Isalnum(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x06)
#define sqlite3Isalpha(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x02)
#define sqlite3Isdigit(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x04)
#define sqlite3Isxdigit(x)  (sqlite3CtypeMap[(unsigned char)(x)]&0x08)
#define sqlite3Tolower(x)   (sqlite3UpperToLower[(unsigned char)(x)])
#define sqlite3Isquote(x)   (sqlite3CtypeMap[(unsigned char)(x)]&0x80)

/*
 * Internal function prototypes
 */
int sqlite3StrICmp(const char *, const char *);
unsigned sqlite3Strlen30(const char *);
#define sqlite3StrNICmp sqlite3_strnicmp

void sqlite3MallocInit(void);
void sqlite3MallocEnd(void);
void *sqlite3Malloc(u64);
void *sqlite3MallocZero(u64);
void *sqlite3DbMallocZero(sqlite3 *, u64);
void *sqlite3DbMallocRaw(sqlite3 *, u64);
void *sqlite3DbMallocRawNN(sqlite3 *, u64);
char *sqlite3DbStrDup(sqlite3 *, const char *);
char *sqlite3DbStrNDup(sqlite3 *, const char *, u64);
void *sqlite3Realloc(void *, u64);
void *sqlite3DbReallocOrFree(sqlite3 *, void *, u64);
void *sqlite3DbRealloc(sqlite3 *, void *, u64);
void sqlite3DbFree(sqlite3 *, void *);
int sqlite3MallocSize(void *);
int sqlite3DbMallocSize(sqlite3 *, void *);
void *sqlite3ScratchMalloc(int);
void sqlite3ScratchFree(void *);
void *sqlite3PageMalloc(int);
void sqlite3PageFree(void *);
void sqlite3MemSetDefault(void);
#ifndef SQLITE_UNTESTABLE
void sqlite3BenignMallocHooks(void (*)(void), void (*)(void));
#endif
int sqlite3HeapNearlyFull(void);

/*
 * On systems with ample stack space and that support alloca(), make
 * use of alloca() to obtain space for large automatic objects.  By default,
 * obtain space from malloc().
 *
 * The alloca() routine never returns NULL.  This will cause code paths
 * that deal with sqlite3StackAlloc() failures to be unreachable.
 */
#ifdef SQLITE_USE_ALLOCA
#define sqlite3StackAllocRaw(D,N)   alloca(N)
#define sqlite3StackAllocZero(D,N)  memset(alloca(N), 0, N)
#define sqlite3StackFree(D,P)
#else
#define sqlite3StackAllocRaw(D,N)   sqlite3DbMallocRaw(D,N)
#define sqlite3StackAllocZero(D,N)  sqlite3DbMallocZero(D,N)
#define sqlite3StackFree(D,P)       sqlite3DbFree(D,P)
#endif

sqlite3_int64 sqlite3StatusValue(int);
void sqlite3StatusUp(int, int);
void sqlite3StatusDown(int, int);
void sqlite3StatusHighwater(int, int);

#ifndef SQLITE_OMIT_FLOATING_POINT
int sqlite3IsNaN(double);
#else
#define sqlite3IsNaN(X)  0
#endif

/*
 * An instance of the following structure holds information about SQL
 * functions arguments that are the parameters to the printf() function.
 */
struct PrintfArguments {
	int nArg;		/* Total number of arguments */
	int nUsed;		/* Number of arguments used so far */
	sqlite3_value **apArg;	/* The argument values */
};

void sqlite3VXPrintf(StrAccum *, const char *, va_list);
void sqlite3XPrintf(StrAccum *, const char *, ...);
char *sqlite3MPrintf(sqlite3 *, const char *, ...);
char *sqlite3VMPrintf(sqlite3 *, const char *, va_list);
#if defined(SQLITE_DEBUG)
void sqlite3DebugPrintf(const char *, ...);
#endif
#if defined(SQLITE_TEST)
void *sqlite3TestTextToPtr(const char *);
#endif

#if defined(SQLITE_DEBUG)
void sqlite3TreeViewExpr(TreeView *, const Expr *, u8);
void sqlite3TreeViewBareExprList(TreeView *, const ExprList *, const char *);
void sqlite3TreeViewExprList(TreeView *, const ExprList *, u8, const char *);
void sqlite3TreeViewSelect(TreeView *, const Select *, u8);
void sqlite3TreeViewWith(TreeView *, const With *);
#endif

void sqlite3SetString(char **, sqlite3 *, const char *);
void sqlite3ErrorMsg(Parse *, const char *, ...);
void sqlite3Dequote(char *);
void sqlite3NormalizeName(char *z);
void sqlite3TokenInit(Token *, char *);
int sqlite3KeywordCode(const unsigned char *, int);
int sqlite3RunParser(Parse *, const char *, char **);
void sqlite3FinishCoding(Parse *);
int sqlite3GetTempReg(Parse *);
void sqlite3ReleaseTempReg(Parse *, int);
int sqlite3GetTempRange(Parse *, int);
void sqlite3ReleaseTempRange(Parse *, int, int);
void sqlite3ClearTempRegCache(Parse *);
#ifdef SQLITE_DEBUG
int sqlite3NoTempsInRange(Parse *, int, int);
#endif
Expr *sqlite3ExprAlloc(sqlite3 *, int, const Token *, int);
Expr *sqlite3Expr(sqlite3 *, int, const char *);
Expr *sqlite3ExprInteger(sqlite3 *, int);
void sqlite3ExprAttachSubtrees(sqlite3 *, Expr *, Expr *, Expr *);
Expr *sqlite3PExpr(Parse *, int, Expr *, Expr *);
void sqlite3PExprAddSelect(Parse *, Expr *, Select *);
Expr *sqlite3ExprAnd(sqlite3 *, Expr *, Expr *);
Expr *sqlite3ExprFunction(Parse *, ExprList *, Token *);
void sqlite3ExprAssignVarNumber(Parse *, Expr *, u32);
ExprList *sqlite3ExprListAppend(Parse *, ExprList *, Expr *);
ExprList *sqlite3ExprListAppendVector(Parse *, ExprList *, IdList *, Expr *);

/**
 * Set the sort order for the last element on the given ExprList.
 *
 * @param p Expression list.
 * @param sort_order Sort order to set.
 */
void sqlite3ExprListSetSortOrder(ExprList *, enum sort_order sort_order);

void sqlite3ExprListSetName(Parse *, ExprList *, Token *, int);
void sqlite3ExprListSetSpan(Parse *, ExprList *, ExprSpan *);
void sqlite3ExprListDelete(sqlite3 *, ExprList *);
u32 sqlite3ExprListFlags(const ExprList *);
int sqlite3Init(sqlite3 *);
int sqlite3InitCallback(void *, int, char **, char **);
void sqlite3Pragma(Parse *, Token *, Token *, Token *, int);
void sqlite3ResetAllSchemasOfConnection(sqlite3 *);
void sqlite3CommitInternalChanges();
void sqlite3DeleteColumnNames(sqlite3 *, Table *);
bool table_column_is_in_pk(Table *, uint32_t);

/**
 * Given an expression list (which is really the list of expressions
 * that form the result set of a SELECT statement) compute appropriate
 * column names for a table that would hold the expression list.
 * All column names will be unique.
 * Initialize fields and field_count.
 *
 * @param parse Parsing context.
 * @param expr_list  Expr list from which to derive column names.
 * @param table Destination table.
 * @retval SQLITE_OK on success.
 * @retval error codef on error.
 */
int sqlite3ColumnsFromExprList(Parse *parse, ExprList *expr_list, Table *table);

void sqlite3SelectAddColumnTypeAndCollation(Parse *, Table *, Select *);
Table *sqlite3ResultSetOfSelect(Parse *, Select *);
Index *sqlite3PrimaryKeyIndex(Table *);
void sqlite3StartTable(Parse *, Token *, int);
void sqlite3AddColumn(Parse *, Token *, Token *);
void sqlite3AddNotNull(Parse *, int);
void sqlite3AddPrimaryKey(Parse *, ExprList *, int, int, enum sort_order);
void sqlite3AddCheckConstraint(Parse *, Expr *);
void sqlite3AddDefaultValue(Parse *, ExprSpan *);
void sqlite3AddCollateType(Parse *, Token *);

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
/**
 * Return name of given column collation from index.
 *
 * @param idx Index which is used to fetch column.
 * @param column Number of column.
 * @param[out] coll_id Collation identifier.
 * @retval Pointer to collation.
 */
struct coll *
sql_index_collation(Index *idx, uint32_t column, uint32_t *id);
bool
space_is_view(Table *);

/**
 * Return key_def of provided struct Index.
 * @param idx Pointer to `struct Index` object.
 * @retval Pointer to `struct key_def`.
 */
struct key_def*
sql_index_key_def(struct Index *idx);

/**
 * Return sort order of given column from index.
 *
 * @param idx Index which is used to fetch column.
 * @param column Number of column.
 * @retval Sort order of requested column.
 */
enum sort_order
sql_index_column_sort_order(Index *idx, uint32_t column);

void sqlite3EndTable(Parse *, Token *, Token *, Select *);
int
emit_open_cursor(Parse *, int, int);

int sqlite3ParseUri(const char *, const char *, unsigned int *,
		    sqlite3_vfs **, char **, char **);

#ifdef SQLITE_UNTESTABLE
#define sqlite3FaultSim(X) SQLITE_OK
#else
int sqlite3FaultSim(int);
#endif

void sqlite3CreateView(Parse *, Token *, Token *, ExprList *, Select *, int);

/**
 * The Table structure pTable is really a VIEW.  Fill in the names
 * of the columns of the view in the table structure.  Return the
 * number of errors.  If an error is seen leave an error message
 * in parse->zErrMsg.
 *
 * @param parse Parsing context.
 * @param table Tables to process.
 * @retval 0 if success, -1 in case of error.
 */
int
sql_view_column_names(struct Parse *parse, struct Table *table);

void
sql_drop_table(struct Parse *, struct SrcList *, bool, bool);
void sqlite3DeleteTable(sqlite3 *, Table *);
void sqlite3Insert(Parse *, SrcList *, Select *, IdList *,
		   enum on_conflict_action);
void *sqlite3ArrayAllocate(sqlite3 *, void *, int, int *, int *);
IdList *sqlite3IdListAppend(sqlite3 *, IdList *, Token *);
int sqlite3IdListIndex(IdList *, const char *);
SrcList *sqlite3SrcListEnlarge(sqlite3 *, SrcList *, int, int);
SrcList *
sql_alloc_src_list(sqlite3 *db);
SrcList *sqlite3SrcListAppend(sqlite3 *, SrcList *, Token *);
SrcList *sqlite3SrcListAppendFromTerm(Parse *, SrcList *, Token *,
				      Token *, Select *, Expr *, IdList *);
void sqlite3SrcListIndexedBy(Parse *, SrcList *, Token *);
void sqlite3SrcListFuncArgs(Parse *, SrcList *, ExprList *);
int sqlite3IndexedByLookup(Parse *, struct SrcList_item *);
void sqlite3SrcListShiftJoinType(SrcList *);
void sqlite3SrcListAssignCursors(Parse *, SrcList *);
void sqlite3IdListDelete(sqlite3 *, IdList *);
void sqlite3SrcListDelete(sqlite3 *, SrcList *);
Index *sqlite3AllocateIndexObject(sqlite3 *, i16, int, char **);
bool
index_is_unique(Index *);

/**
 * Create a new index for an SQL table.  name is the name of the
 * index and tbl_name is the name of the table that is to be
 * indexed.  Both will be NULL for a primary key or an index that
 * is created to satisfy a UNIQUE constraint.  If tbl_name and
 * name are NULL, use parse->pNewTable as the table to be indexed.
 * parse->pNewTable is a table that is currently being
 * constructed by a CREATE TABLE statement.
 *
 * col_list is a list of columns to be indexed.  col_list will be
 * NULL if this is a primary key or unique-constraint on the most
 * recent column added to the table currently under construction.
 *
 * @param parse All information about this parse.
 * @param token Index name. May be NULL.
 * @param tbl_name Table to index. Use pParse->pNewTable ifNULL.
 * @param col_list A list of columns to be indexed.
 * @param on_error One of ON_CONFLICT_ACTION_ABORT, _IGNORE,
 *        _REPLACE, or _NONE.
 * @param start The CREATE token that begins this statement.
 * @param pi_where WHERE clause for partial indices.
 * @param sort_order Sort order of primary key when pList==NULL.
 * @param if_not_exist Omit error if index already exists.
 * @param idx_type The index type.
 */
void
sql_create_index(struct Parse *parse, struct Token *token,
		 struct SrcList *tbl_name, struct ExprList *col_list,
		 int on_error, struct Token *start, struct Expr *pi_where,
		 enum sort_order sort_order, bool if_not_exist, u8 idx_type);

/**
 * This routine will drop an existing named index.  This routine
 * implements the DROP INDEX statement.
 *
 * @param parse_context Current parsing context.
 * @param index_name_list List containing index name.
 * @param table_token Token representing table name.
 * @param if_exists True, if statement contains 'IF EXISTS' clause.
 */
void
sql_drop_index(struct Parse *, struct SrcList *, struct Token *, bool);

int sqlite3Select(Parse *, Select *, SelectDest *);
Select *sqlite3SelectNew(Parse *, ExprList *, SrcList *, Expr *, ExprList *,
			 Expr *, ExprList *, u32, Expr *, Expr *);
void sqlite3SelectDelete(sqlite3 *, Select *);

/**
 * While a SrcList can in general represent multiple tables and
 * subqueries (as in the FROM clause of a SELECT statement) in
 * this case it contains the name of a single table, as one might
 * find in an INSERT, DELETE, or UPDATE statement.  Look up that
 * table in the symbol table and return a pointer.  Set an error
 * message and return NULL if the table name is not found or if
 * any other error occurs.
 *
 * The following fields are initialized appropriate in src_list:
 *
 *    src_list->a[0].pTab       Pointer to the Table object.
 *    src_list->a[0].pIndex     Pointer to the INDEXED BY index,
 *                              if there is one.
 *
 * @param parse Parsing context.
 * @param src_list List containing single table element.
 * @retval Table object if found, NULL oterwise.
 */
struct Table *
sql_list_lookup_table(struct Parse *parse, struct SrcList *src_list);

void sqlite3OpenTable(Parse *, int iCur, Table *, int);
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

void sqlite3Update(Parse *, SrcList *, ExprList *, Expr *,
		   enum on_conflict_action);
WhereInfo *sqlite3WhereBegin(Parse *, SrcList *, Expr *, ExprList *, ExprList *,
			     u16, int);
void sqlite3WhereEnd(WhereInfo *);
LogEst sqlite3WhereOutputRowCount(WhereInfo *);
int sqlite3WhereIsDistinct(WhereInfo *);
int sqlite3WhereIsOrdered(WhereInfo *);
int sqlite3WhereOrderedInnerLoop(WhereInfo *);
int sqlite3WhereIsSorted(WhereInfo *);
int sqlite3WhereContinueLabel(WhereInfo *);
int sqlite3WhereBreakLabel(WhereInfo *);
int sqlite3WhereOkOnePass(WhereInfo *, int *);
#define ONEPASS_OFF      0	/* Use of ONEPASS not allowed */
#define ONEPASS_SINGLE   1	/* ONEPASS valid for a single row update */
#define ONEPASS_MULTI    2	/* ONEPASS is valid for multiple rows */
void sqlite3ExprCodeLoadIndexColumn(Parse *, Index *, int, int, int);

/**
 * Generate code that will extract the iColumn-th column from
 * table pTab and store the column value in a register.
 *
 * An effort is made to store the column value in register iReg.
 * This is not garanteeed for GetColumn() - the result can be
 * stored in any register.  But the result is guaranteed to land
 * in register iReg for GetColumnToReg().
 * @param pParse Parsing and code generating context.
 * @param space_def Space definition.
 * @param iColumn Index of the table column.
 * @param iTable The cursor pointing to the table.
 * @param iReg Store results here.
 * @param p5 P5 value for OP_Column + FLAGS.
 * @return iReg value.
 */
int
sqlite3ExprCodeGetColumn(Parse *, struct space_def *, int, int, int, u8);

/**
 * Generate code that will extract the iColumn-th column from
 * table pTab and store the column value in a register, copy the
 * result.
 * @param pParse Parsing and code generating context.
 * @param space_def Space definition.
 * @param iColumn Index of the table column.
 * @param iTable The cursor pointing to the table.
 * @param iReg Store results here.
 */
void
sqlite3ExprCodeGetColumnToReg(Parse *, struct space_def *, int, int, int);

/**
 * Generate code to extract the value of the iCol-th column of a table.
 * @param v  The VDBE under construction.
 * @param space_def Space definition.
 * @param iTabCur The PK cursor.
 * @param iCol Index of the column to extract.
 * @param regOut  Extract the value into this register.
 */
void
sqlite3ExprCodeGetColumnOfTable(Vdbe *, struct space_def *, int, int, int);

void sqlite3ExprCodeMove(Parse *, int, int, int);
void sqlite3ExprCacheStore(Parse *, int, int, int);
void sqlite3ExprCachePush(Parse *);
void sqlite3ExprCachePop(Parse *);
void sqlite3ExprCacheRemove(Parse *, int, int);
void sqlite3ExprCacheClear(Parse *);
void sqlite3ExprCacheAffinityChange(Parse *, int, int);
void sqlite3ExprCode(Parse *, Expr *, int);
void sqlite3ExprCodeCopy(Parse *, Expr *, int);
void sqlite3ExprCodeFactorable(Parse *, Expr *, int);
void sqlite3ExprCodeAtInit(Parse *, Expr *, int, u8);
int sqlite3ExprCodeTemp(Parse *, Expr *, int *);
int sqlite3ExprCodeTarget(Parse *, Expr *, int);
void sqlite3ExprCodeAndCache(Parse *, Expr *, int);
int sqlite3ExprCodeExprList(Parse *, ExprList *, int, int, u8);
#define SQLITE_ECEL_DUP      0x01	/* Deep, not shallow copies */
#define SQLITE_ECEL_FACTOR   0x02	/* Factor out constant terms */
#define SQLITE_ECEL_REF      0x04	/* Use ExprList.u.x.iOrderByCol */
#define SQLITE_ECEL_OMITREF  0x08	/* Omit if ExprList.u.x.iOrderByCol */
void sqlite3ExprIfTrue(Parse *, Expr *, int, int);
void sqlite3ExprIfFalse(Parse *, Expr *, int, int);
void sqlite3ExprIfFalseDup(Parse *, Expr *, int, int);
#define LOCATE_VIEW    0x01
#define LOCATE_NOERR   0x02
Table *sqlite3LocateTable(Parse *, u32 flags, const char *);
Index *sqlite3LocateIndex(sqlite3 *, const char *, const char *);
void sqlite3UnlinkAndDeleteTable(sqlite3 *, const char *);
void sqlite3UnlinkAndDeleteIndex(sqlite3 *, Index *);
char *sqlite3NameFromToken(sqlite3 *, Token *);
int sqlite3ExprCompare(Expr *, Expr *, int);
int sqlite3ExprListCompare(ExprList *, ExprList *, int);
int sqlite3ExprImpliesExpr(Expr *, Expr *, int);
void sqlite3ExprAnalyzeAggregates(NameContext *, Expr *);
void sqlite3ExprAnalyzeAggList(NameContext *, ExprList *);
int sqlite3ExprCoveredByIndex(Expr *, int iCur, Index * pIdx);
int sqlite3FunctionUsesThisSrc(Expr *, SrcList *);
Vdbe *sqlite3GetVdbe(Parse *);
#ifndef SQLITE_UNTESTABLE
void sqlite3PrngSaveState(void);
void sqlite3PrngRestoreState(void);
#endif
void sqlite3RollbackAll(Vdbe *, int);

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

void sqlite3Savepoint(Parse *, int, Token *);
void sqlite3CloseSavepoints(Vdbe *);
int sqlite3ExprIsConstant(Expr *);
int sqlite3ExprIsConstantNotJoin(Expr *);
int sqlite3ExprIsConstantOrFunction(Expr *, u8);
int sqlite3ExprIsTableConstant(Expr *, int);
#ifdef SQLITE_ENABLE_CURSOR_HINTS
int sqlite3ExprContainsSubquery(Expr *);
#endif
int sqlite3ExprIsInteger(Expr *, int *);
int sqlite3ExprCanBeNull(const Expr *);
int sqlite3ExprNeedsNoAffinityChange(const Expr *, char);

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
 * @param table Table containing the row to be deleted.
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
sql_generate_row_delete(struct Parse *parse, struct Table *table,
			struct Trigger *trigger_list, int cursor, int reg_pk,
			short npk, bool need_update_count,
			enum on_conflict_action onconf, u8 mode,
			int idx_noseek);

/**
 * Generate code that will assemble an index key and stores it in
 * register reg_out. The key will be for index which is an
 * index on table. cursor is the index of a cursor open on the
 * table table and pointing to the entry that needs indexing.
 * cursor must be the cursor of the PRIMARY KEY index.
 *
 * If part_idx_label is not NULL, fill it in with a label and
 * jump to that label if index is a partial index that should be
 * skipped. The label should be resolved using
 * sql_resolve_part_idx_label(). A partial index should be skipped
 * if its WHERE clause evaluates to false or null.  If index is
 * not a partial index, *part_idx_label will be set to zero which
 * is an empty label that is ignored by
 * sql_resolve_part_idx_label().
 *
 * The prev and reg_prev parameters are used to implement a
 * cache to avoid unnecessary register loads.  If prev is not
 * NULL, then it is a pointer to a different index for which an
 * index key has just been computed into register reg_prev. If the
 * current index is generating its key into the same
 * sequence of registers and if prev and index share a column in
 * common, then the register corresponding to that column already
 * holds the correct value and the loading of that register is
 * skipped. This optimization is helpful when doing a DELETE or
 * an INTEGRITY_CHECK on a table with multiple indices, and
 * especially with the PRIMARY KEY columns of the index.
 *
 * @param parse Parsing context.
 * @param index The index for which to generate a key.
 * @param cursor Cursor number from which to take column data.
 * @param reg_out Put the new key into this register if not NULL.
 * @param[out] part_idx_label Jump to this label to skip partial
 *        index.
 * @param prev Previously generated index key
 * @param reg_prev Register holding previous generated key.
 *
 * @retval Return a register number which is the first in a block
 * of registers that holds the elements of the index key. The
 * block of registers has already been deallocated by the time
 * this routine returns.
 */
int
sql_generate_index_key(struct Parse *parse, struct Index *index, int cursor,
		       int reg_out, int *part_idx_label, struct Index *prev,
		       int reg_prev);

/**
 * If a prior call to sql_generate_index_key() generated a
 * jump-over label because it was a partial index, then this
 * routine should be called to resolve that label.
 *
 * @param parse Parsing context.
 * @param label Label to resolve.
 */
void
sql_resolve_part_idx_label(struct Parse *parse, int label);

void sqlite3GenerateConstraintChecks(Parse *, Table *, int *, int, int, int,
				     int, u8, struct on_conflict *, int, int *,
				     int *);
/**
 * This routine generates code to finish the INSERT or UPDATE
 * operation that was started by a prior call to
 * sqlite3GenerateConstraintChecks.
 * @param v Virtual database engine.
 * @param cursor_id Primary index cursor.
 * @param tuple_id Register with data to insert.
 * @param on_conflict Structure, which contains override error
 * 	  action on failed insert/replaceand and optimized error
 * 	  action after generating bytecode for constraints checks.
 */
void
vdbe_emit_insertion_completion(Vdbe *v, int cursor_id, int tuple_id,
			       struct on_conflict *on_conflict);

int sqlite3OpenTableAndIndices(Parse *, Table *, int, u8, int, u8 *, int *,
			       int *, u8, u8);
void
sql_set_multi_write(Parse *, bool);
void sqlite3MayAbort(Parse *);
void sqlite3HaltConstraint(Parse *, int, int, char *, i8, u8);
void sqlite3UniqueConstraint(Parse *, int, Index *);
Expr *sqlite3ExprDup(sqlite3 *, Expr *, int);
ExprList *sqlite3ExprListDup(sqlite3 *, ExprList *, int);
SrcList *sqlite3SrcListDup(sqlite3 *, SrcList *, int);
IdList *sqlite3IdListDup(sqlite3 *, IdList *);
Select *sqlite3SelectDup(sqlite3 *, Select *, int);
#ifdef SELECTTRACE_ENABLED
void sqlite3SelectSetName(Select *, const char *);
#else
#define sqlite3SelectSetName(A,B)
#endif
void sqlite3InsertBuiltinFuncs(FuncDef *, int);
FuncDef *sqlite3FindFunction(sqlite3 *, const char *, int, u8);
void sqlite3RegisterBuiltinFunctions(void);
void sqlite3RegisterDateTimeFunctions(void);
void sqlite3RegisterPerConnectionBuiltinFunctions(sqlite3 *);
int sqlite3SafetyCheckOk(sqlite3 *);
int sqlite3SafetyCheckSickOrOk(sqlite3 *);
void sqlite3ChangeCookie(Parse *);

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

#ifndef SQLITE_OMIT_TRIGGER
void sqlite3BeginTrigger(Parse *, Token *, int, int, IdList *, SrcList *,
			 Expr *, int);
void sqlite3FinishTrigger(Parse *, TriggerStep *, Token *);
void sqlite3DropTrigger(Parse *, SrcList *, int);
void sqlite3DropTriggerPtr(Parse *, Trigger *);
Trigger *sqlite3TriggersExist(Table *, int, ExprList *, int *pMask);
void sqlite3CodeRowTrigger(Parse *, Trigger *, int, ExprList *, int, Table *,
			   int, int, int);
void sqlite3CodeRowTriggerDirect(Parse *, Trigger *, Table *, int, int, int);
void sqliteViewTriggers(Parse *, Table *, Expr *, int, ExprList *);
void sqlite3DeleteTriggerStep(sqlite3 *, TriggerStep *);
TriggerStep *sqlite3TriggerSelectStep(sqlite3 *, Select *);
TriggerStep *sqlite3TriggerInsertStep(sqlite3 *, Token *, IdList *,
				      Select *, u8);
TriggerStep *sqlite3TriggerUpdateStep(sqlite3 *, Token *, ExprList *, Expr *,
				      u8);
TriggerStep *sqlite3TriggerDeleteStep(sqlite3 *, Token *, Expr *);
void sqlite3DeleteTrigger(sqlite3 *, Trigger *);
void sqlite3UnlinkAndDeleteTrigger(sqlite3 *, const char *);
u32 sqlite3TriggerColmask(Parse *, Trigger *, ExprList *, int, int, Table *,
			  int);
#define sqlite3ParseToplevel(p) ((p)->pToplevel ? (p)->pToplevel : (p))
#define sqlite3IsToplevel(p) ((p)->pToplevel==0)
#else
#define sqlite3TriggersExist(C,D,E,F) 0
#define sqlite3DeleteTrigger(A,B)
#define sqlite3DropTriggerPtr(A,B)
#define sqlite3UnlinkAndDeleteTrigger(A,B,C)
#define sqlite3CodeRowTrigger(A,B,C,D,E,F,G,H,I)
#define sqlite3CodeRowTriggerDirect(A,B,C,D,E,F)
#define sqlite3ParseToplevel(p) p
#define sqlite3IsToplevel(p) 1
#define sqlite3TriggerColmask(A,B,C,D,E,F,G) 0
#endif

int sqlite3JoinType(Parse *, Token *, Token *, Token *);
void sqlite3CreateForeignKey(Parse *, ExprList *, Token *, ExprList *, int);
void sqlite3DeferForeignKey(Parse *, int);
void sqlite3Detach(Parse *, Expr *);
void sqlite3FixInit(DbFixer *, Parse *, const char *, const Token *);
int sqlite3FixSrcList(DbFixer *, SrcList *);
int sqlite3FixSelect(DbFixer *, Select *);
int sqlite3FixExpr(DbFixer *, Expr *);
int sqlite3FixExprList(DbFixer *, ExprList *);
int sqlite3FixTriggerStep(DbFixer *, TriggerStep *);
int sqlite3AtoF(const char *z, double *, int);
int sqlite3GetInt32(const char *, int *);
int sqlite3Atoi(const char *);
int sqlite3Utf8CharLen(const char *pData, int nByte);
u32 sqlite3Utf8Read(const u8 **);
LogEst sqlite3LogEst(u64);
LogEst sqlite3LogEstAdd(LogEst, LogEst);
u64 sqlite3LogEstToInt(LogEst);
VList *sqlite3VListAdd(sqlite3 *, VList *, const char *, int, int);
const char *sqlite3VListNumToName(VList *, int);
int sqlite3VListNameToNum(VList *, const char *, int);

/*
 * Routines to read and write variable-length integers.  These used to
 * be defined locally, but now we use the varint routines in the util.c
 * file.
 */
int sqlite3PutVarint(unsigned char *, u64);
u8 sqlite3GetVarint(const unsigned char *, u64 *);
u8 sqlite3GetVarint32(const unsigned char *, u32 *);
int sqlite3VarintLen(u64 v);

/*
 * The common case is for a varint to be a single byte.  They following
 * macros handle the common case without a procedure call, but then call
 * the procedure for larger varints.
 */
#define getVarint32(A,B)  \
  (u8)((*(A)<(u8)0x80)?((B)=(u32)*(A)),1:sqlite3GetVarint32((A),(u32 *)&(B)))
#define putVarint32(A,B)  \
  (u8)(((u32)(B)<(u32)0x80)?(*(A)=(unsigned char)(B)),1:\
  sqlite3PutVarint((A),(B)))
#define getVarint    sqlite3GetVarint
#define putVarint    sqlite3PutVarint

const char *sqlite3IndexAffinityStr(sqlite3 *, Index *);
void sqlite3TableAffinity(Vdbe *, Table *, int);
char sqlite3CompareAffinity(Expr * pExpr, char aff2);
int sqlite3IndexAffinityOk(Expr * pExpr, char idx_affinity);

/**
 * Return the affinity character for a single column of a table.
 * @param def space definition.
 * @param idx column index.
 * @retval AFFINITY
 */
char
sqlite3TableColumnAffinity(struct space_def *def, int idx);

char sqlite3ExprAffinity(Expr * pExpr);
int sqlite3Atoi64(const char *, i64 *, int);
int sqlite3DecOrHexToI64(const char *, i64 *);
void sqlite3ErrorWithMsg(sqlite3 *, int, const char *, ...);
void sqlite3Error(sqlite3 *, int);
void sqlite3SystemError(sqlite3 *, int);
void *sqlite3HexToBlob(sqlite3 *, const char *z, int n);
u8 sqlite3HexToInt(int h);

#if defined(SQLITE_NEED_ERR_NAME)
const char *sqlite3ErrName(int);
#endif

const char *sqlite3ErrStr(int);

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
 * @param[out] is_found Flag set if collation was found.
 * @param[out] coll_id Collation identifier.
 *
 * @retval Pointer to collation.
 */
struct coll *
sql_expr_coll(Parse * pParse, Expr * pExpr, bool *is_found, uint32_t *coll_id);

Expr *sqlite3ExprAddCollateToken(Parse * pParse, Expr *, const Token *, int);
Expr *sqlite3ExprAddCollateString(Parse *, Expr *, const char *);
Expr *sqlite3ExprSkipCollate(Expr *);
int sqlite3CheckIdentifierName(Parse *, char *);
void sqlite3VdbeSetChanges(sqlite3 *, int);
int sqlite3AddInt64(i64 *, i64);
int sqlite3SubInt64(i64 *, i64);
int sqlite3MulInt64(i64 *, i64);
int sqlite3AbsInt32(int);
#ifdef SQLITE_ENABLE_8_3_NAMES
void sqlite3FileSuffix3(const char *, char *);
#else
#define sqlite3FileSuffix3(X,Y)
#endif
u8 sqlite3GetBoolean(const char *z, u8);

const void *sqlite3ValueText(sqlite3_value *);
int sqlite3ValueBytes(sqlite3_value *);
void sqlite3ValueSetStr(sqlite3_value *, int, const void *,
			void (*)(void *));
void sqlite3ValueSetNull(sqlite3_value *);
void sqlite3ValueFree(sqlite3_value *);
sqlite3_value *sqlite3ValueNew(sqlite3 *);
int sqlite3ValueFromExpr(sqlite3 *, Expr *, u8, sqlite3_value **);
void sqlite3ValueApplyAffinity(sqlite3_value *, u8);
#ifndef SQLITE_AMALGAMATION
extern const unsigned char sqlite3OpcodeProperty[];
extern const char sqlite3StrBINARY[];
extern const unsigned char sqlite3UpperToLower[];
extern const unsigned char sqlite3CtypeMap[];
extern const Token sqlite3IntTokens[];
extern SQLITE_WSD struct Sqlite3Config sqlite3Config;
extern FuncDefHash sqlite3BuiltinFunctions;
#ifndef SQLITE_OMIT_WSD
extern int sqlite3PendingByte;
#endif
#endif
void sqlite3Reindex(Parse *, Token *, Token *);
void sqlite3AlterRenameTable(Parse *, SrcList *, Token *);

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

void sqlite3NestedParse(Parse *, const char *, ...);
void sqlite3ExpirePreparedStatements(sqlite3 *);
int sqlite3CodeSubselect(Parse *, Expr *, int);
void sqlite3SelectPrep(Parse *, Select *, NameContext *);

/**
 * Error message for when two or more terms of a compound select
 * have different size result sets.
 *
 * @param parse Parsing context.
 * @param p Select struct to analyze.
 */
void
sqlite3SelectWrongNumTermsError(struct Parse *parse, struct Select *p);

int sqlite3MatchSpanName(const char *, const char *, const char *);
int sqlite3ResolveExprNames(NameContext *, Expr *);
int sqlite3ResolveExprListNames(NameContext *, ExprList *);
void sqlite3ResolveSelectNames(Parse *, Select *, NameContext *);
void sqlite3ResolveSelfReference(Parse *, Table *, int, Expr *, ExprList *);
int sqlite3ResolveOrderGroupBy(Parse *, Select *, ExprList *, const char *);

/**
 * Generate code for default value.
 * The most recently coded instruction was an OP_Column to retrieve the
 * i-th column of table pTab. This routine sets the P4 parameter of the
 * OP_Column to the default value, if any.
 *
 * The default value of a column is specified by a DEFAULT clause in the
 * column definition. This was either supplied by the user when the table
 * was created, or added later to the table definition by an ALTER TABLE
 * command. If the latter, then the row-records in the table btree on disk
 * may not contain a value for the column and the default value, taken
 * from the P4 parameter of the OP_Column instruction, is returned instead.
 * If the former, then all row-records are guaranteed to include a value
 * for the column and the P4 value is not required.
 *
 * Column definitions created by an ALTER TABLE command may only have
 * literal default values specified: a number, null or a string. (If a more
 * complicated default expression value was provided, it is evaluated
 * when the ALTER TABLE is executed and one of the literal values written
 * into the schema.)
 *
 * Therefore, the P4 parameter is only required if the default value for
 * the column is a literal number, string or null. The sqlite3ValueFromExpr()
 * function is capable of transforming these types of expressions into
 * sqlite3_value objects.
 *
 * If parameter iReg is not negative, code an OP_RealAffinity instruction
 * on register iReg. This is used when an equivalent integer value is
 * stored in place of an 8-byte floating point value in order to save
 * space.
 * @param v Vdbe object.
 * @param def space definition object.
 * @param i column index.
 * @param iReg register index.
 */
void
sqlite3ColumnDefault(Vdbe *v, struct space_def *def, int i, int ireg);

void sqlite3AlterFinishAddColumn(Parse *, Token *);
void sqlite3AlterBeginAddColumn(Parse *, SrcList *);
char* rename_table(sqlite3 *, const char *, const char *, bool *);
char* rename_parent_table(sqlite3 *, const char *, const char *, const char *,
			  uint32_t *, uint32_t *);
char* rename_trigger(sqlite3 *, char const *, char const *, bool *);
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
char sqlite3AffinityType(const char *, u8 *);
void sqlite3Analyze(Parse *, Token *);

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

int sqlite3InvokeBusyHandler(BusyHandler *);

/**
 * Load the content of the _sql_stat1 and sql_stat4 tables. The
 * contents of _sql_stat1 are used to populate the tuple_stat1[]
 * arrays. The contents of sql_stat4 are used to populate the
 * samples[] arrays.
 *
 * @param db Database handler.
 * @retval SQLITE_OK on success, smth else otherwise.
 */
int
sql_analysis_load(struct sqlite3 *db);

uint32_t
index_column_count(const Index *);
bool
index_is_unique_not_null(const Index *);
void sqlite3RegisterLikeFunctions(sqlite3 *, int);
int sqlite3IsLikeFunction(sqlite3 *, Expr *, int *, char *);
void sqlite3SchemaClear(sqlite3 *);
Schema *sqlite3SchemaCreate(sqlite3 *);
int sqlite3CreateFunc(sqlite3 *, const char *, int, int, void *,
		      void (*)(sqlite3_context *, int, sqlite3_value **),
		      void (*)(sqlite3_context *, int, sqlite3_value **),
		      void (*)(sqlite3_context *),
		      FuncDestructor * pDestructor);
void sqlite3OomFault(sqlite3 *);
void sqlite3OomClear(sqlite3 *);
int sqlite3ApiExit(sqlite3 * db, int);

void sqlite3StrAccumInit(StrAccum *, sqlite3 *, char *, int, int);
void sqlite3StrAccumAppend(StrAccum *, const char *, int);
void sqlite3StrAccumAppendAll(StrAccum *, const char *);
void sqlite3AppendChar(StrAccum *, int, char);
char *sqlite3StrAccumFinish(StrAccum *);
void sqlite3StrAccumReset(StrAccum *);
void sqlite3SelectDestInit(SelectDest *, int, int);
Expr *sqlite3CreateColumnExpr(sqlite3 *, SrcList *, int, int);

int sqlite3ExprCheckIN(Parse *, Expr *);

void sqlite3AnalyzeFunctions(void);
int sqlite3Stat4ProbeSetValue(Parse *, Index *, UnpackedRecord **, Expr *, int,
			      int, int *);
int sqlite3Stat4ValueFromExpr(Parse *, Expr *, u8, sqlite3_value **);
void sqlite3Stat4ProbeFree(UnpackedRecord *);
int sqlite3Stat4Column(sqlite3 *, const void *, int, int, sqlite3_value **);
char sqlite3IndexColumnAffinity(sqlite3 *, Index *, int);

/*
 * The interface to the LEMON-generated parser
 */
void *sqlite3ParserAlloc(void *(*)(u64));
void sqlite3ParserFree(void *, void (*)(void *));
void sqlite3Parser(void *, int, Token, Parse *);
#ifdef YYTRACKMAXSTACKDEPTH
int sqlite3ParserStackPeak(void *);
#endif

#ifdef SQLITE_TEST
int sqlite3Utf8To8(unsigned char *);
#endif

void sqlite3InvalidFunction(sqlite3_context *, int, sqlite3_value **);
sqlite3_int64 sqlite3StmtCurrentTime(sqlite3_context *);
int sqlite3VdbeParameterIndex(Vdbe *, const char *, int);
int sqlite3TransferBindings(sqlite3_stmt *, sqlite3_stmt *);
int sqlite3Reprepare(Vdbe *);
void sqlite3ExprListCheckLength(Parse *, ExprList *, const char *);
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
 * @param[out] coll_id Collation identifier.
 *
 * @retval Collation object.
 */
struct coll *
sql_binary_compare_coll_seq(Parse *parser, Expr *left, Expr *right,
			    uint32_t *coll_id);
int sqlite3TempInMemory(const sqlite3 *);
#ifndef SQLITE_OMIT_CTE
With *sqlite3WithAdd(Parse *, With *, Token *, ExprList *, Select *);
void sqlite3WithDelete(sqlite3 *, With *);
void sqlite3WithPush(Parse *, With *, u8);
#else
#define sqlite3WithPush(x,y,z)
#define sqlite3WithDelete(x,y)
#endif

/* Declarations for functions in fkey.c. All of these are replaced by
 * no-op macros if OMIT_FOREIGN_KEY is defined. In this case no foreign
 * key functionality is available. If OMIT_TRIGGER is defined but
 * OMIT_FOREIGN_KEY is not, only some of the functions are no-oped. In
 * this case foreign keys are parsed, but no other functionality is
 * provided (enforcement of FK constraints requires the triggers sub-system).
 */
#if !defined(SQLITE_OMIT_FOREIGN_KEY) && !defined(SQLITE_OMIT_TRIGGER)
void sqlite3FkCheck(Parse *, Table *, int, int, int *);
void sqlite3FkDropTable(Parse *, SrcList *, Table *);
void sqlite3FkActions(Parse *, Table *, ExprList *, int, int *);
int sqlite3FkRequired(Table *, int *);
u32 sqlite3FkOldmask(Parse *, Table *);
FKey *sqlite3FkReferences(Table *);
#else
#define sqlite3FkActions(a,b,c,d,e)
#define sqlite3FkCheck(a,b,c,d,e,f)
#define sqlite3FkDropTable(a,b,c)
#define sqlite3FkOldmask(a,b)         0
#define sqlite3FkRequired(b,c)    0
#endif
#ifndef SQLITE_OMIT_FOREIGN_KEY
void sqlite3FkDelete(sqlite3 *, Table *);
int sqlite3FkLocateIndex(Parse *, Table *, FKey *, Index **, int **);
#else
#define sqlite3FkDelete(a,b)
#define sqlite3FkLocateIndex(a,b,c,d,e)
#endif

/*
 * Available fault injectors.  Should be numbered beginning with 0.
 */
#define SQLITE_FAULTINJECTOR_MALLOC     0
#define SQLITE_FAULTINJECTOR_COUNT      1

/*
 * The interface to the code in fault.c used for identifying "benign"
 * malloc failures. This is only present if SQLITE_UNTESTABLE
 * is not defined.
 */
#ifndef SQLITE_UNTESTABLE
void sqlite3BeginBenignMalloc(void);
void sqlite3EndBenignMalloc(void);
#else
#define sqlite3BeginBenignMalloc()
#define sqlite3EndBenignMalloc()
#endif

/*
 * Allowed return values from sqlite3FindInIndex()
 */
#define IN_INDEX_EPH          2	/* Search an ephemeral b-tree */
#define IN_INDEX_INDEX_ASC    3	/* Existing index ASCENDING */
#define IN_INDEX_INDEX_DESC   4	/* Existing index DESCENDING */
#define IN_INDEX_NOOP         5	/* No table available. Use comparisons */
/*
 * Allowed flags for the 3rd parameter to sqlite3FindInIndex().
 */
#define IN_INDEX_NOOP_OK     0x0001	/* OK to return IN_INDEX_NOOP */
#define IN_INDEX_MEMBERSHIP  0x0002	/* IN operator used for membership test */
#define IN_INDEX_LOOP        0x0004	/* IN operator used as a loop */
int sqlite3FindInIndex(Parse *, Expr *, u32, int *, int *, int *);

void sqlite3ExprSetHeightAndFlags(Parse * pParse, Expr * p);
#if SQLITE_MAX_EXPR_DEPTH>0
int sqlite3SelectExprHeight(Select *);
int sqlite3ExprCheckHeight(Parse *, int);
#else
#define sqlite3SelectExprHeight(x) 0
#define sqlite3ExprCheckHeight(x,y)
#endif

u32 sqlite3Get4byte(const u8 *);
void sqlite3Put4byte(u8 *, u32);

#ifdef SQLITE_DEBUG
void sqlite3ParserTrace(FILE *, char *);
#endif

/*
 * If the SQLITE_ENABLE IOTRACE exists then the global variable
 * sqlite3IoTrace is a pointer to a printf-like routine used to
 * print I/O tracing messages.
 */
#ifdef SQLITE_ENABLE_IOTRACE
#define IOTRACE(A)  if( sqlite3IoTrace ){ sqlite3IoTrace A; }
void sqlite3VdbeIOTraceSql(Vdbe *);
 SQLITE_EXTERN void (SQLITE_CDECL * sqlite3IoTrace) (const char *,
							       ...);
#else
#define IOTRACE(A)
#define sqlite3VdbeIOTraceSql(X)
#endif

/*
 * These routines are available for the mem2.c debugging memory allocator
 * only.  They are used to verify that different "types" of memory
 * allocations are properly tracked by the system.
 *
 * sqlite3MemdebugSetType() sets the "type" of an allocation to one of
 * the MEMTYPE_* macros defined below.  The type must be a bitmask with
 * a single bit set.
 *
 * sqlite3MemdebugHasType() returns true if any of the bits in its second
 * argument match the type set by the previous sqlite3MemdebugSetType().
 * sqlite3MemdebugHasType() is intended for use inside assert() statements.
 *
 * sqlite3MemdebugNoType() returns true if none of the bits in its second
 * argument match the type set by the previous sqlite3MemdebugSetType().
 *
 * Perhaps the most important point is the difference between MEMTYPE_HEAP
 * and MEMTYPE_LOOKASIDE.  If an allocation is MEMTYPE_LOOKASIDE, that means
 * it might have been allocated by lookaside, except the allocation was
 * too large or lookaside was already full.  It is important to verify
 * that allocations that might have been satisfied by lookaside are not
 * passed back to non-lookaside free() routines.  Asserts such as the
 * example above are placed on the non-lookaside free() routines to verify
 * this constraint.
 *
 * All of this is no-op for a production build.  It only comes into
 * play when the SQLITE_MEMDEBUG compile-time option is used.
 */
#ifdef SQLITE_MEMDEBUG
void sqlite3MemdebugSetType(void *, u8);
int sqlite3MemdebugHasType(void *, u8);
int sqlite3MemdebugNoType(void *, u8);
#else
#define sqlite3MemdebugSetType(X,Y)	/* no-op */
#define sqlite3MemdebugHasType(X,Y)  1
#define sqlite3MemdebugNoType(X,Y)   1
#endif
#define MEMTYPE_HEAP       0x01	/* General heap allocations */
#define MEMTYPE_LOOKASIDE  0x02	/* Heap that might have been lookaside */
#define MEMTYPE_SCRATCH    0x04	/* Scratch allocations */
#define MEMTYPE_PCACHE     0x08	/* Page cache allocations */

/*
 * Threading interface
 */
#if SQLITE_MAX_WORKER_THREADS>0
int sqlite3ThreadCreate(SQLiteThread **, void *(*)(void *), void *);
int sqlite3ThreadJoin(SQLiteThread *, void **);
#endif

int sqlite3ExprVectorSize(Expr * pExpr);
int sqlite3ExprIsVector(Expr * pExpr);
Expr *sqlite3VectorFieldSubexpr(Expr *, int);
Expr *sqlite3ExprForVectorField(Parse *, Expr *, int);
void sqlite3VectorErrorMsg(Parse *, Expr *);

/* Tarantool: right now query compilation is invoked on top of
 * fiber's stack. Need to limit number of nested programs under
 * compilation to avoid stack overflow.
 */
extern int sqlSubProgramsRemaining;

extern int sqlite3InitDatabase(sqlite3 * db);

enum on_conflict_action
table_column_nullable_action(struct Table *tab, uint32_t column);

#endif				/* SQLITEINT_H */
