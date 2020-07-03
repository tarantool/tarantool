/*
 * Copyright (C) 2000-2010 PostgreSQL Global Development Group
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "proc_title.h"
#include "trivia/config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#ifdef HAVE_SYS_PSTAT_H
#include <sys/pstat.h>		/* for HP-UX */
#endif
#ifdef HAVE_PS_STRINGS
#include <machine/vmparam.h>	/* for old BSD */
#include <sys/exec.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

extern char **environ;

/*
 * Alternative ways of updating ps display:
 *
 * PS_USE_SETPROCTITLE
 *	   use the function setproctitle(const char *, ...)
 *	   (newer BSD systems)
 * PS_USE_PSTAT
 *	   use the pstat(PSTAT_SETCMD, )
 *	   (HPUX)
 * PS_USE_PS_STRINGS
 *	   assign PS_STRINGS->ps_argvstr = "string"
 *	   (some BSD systems)
 * PS_USE_CHANGE_ARGV
 *	   assign argv[0] = "string"
 *	   (some other BSD systems)
 * PS_USE_CLOBBER_ARGV
 *	   write over the argv and environment area
 *	   (most SysV-like systems)
 * PS_USE_WIN32
 *	   push the string out as the name of a Windows event
 * PS_USE_NONE
 *	   don't update ps display
 *	   (This is the default, as it is safest.)
 */
#if defined(HAVE_SETPROCTITLE)
#define PS_USE_SETPROCTITLE
#elif defined(HAVE_PSTAT) && defined(PSTAT_SETCMD)
#define PS_USE_PSTAT
#elif defined(HAVE_PS_STRINGS)
#define PS_USE_PS_STRINGS
#elif (defined(BSD) || defined(__bsdi__) || defined(__hurd__)) && !defined(__APPLE__)
#define PS_USE_CHANGE_ARGV
#elif defined(__linux__) || defined(_AIX) || defined(__sgi) || (defined(sun) && !defined(BSD)) || defined(ultrix) || defined(__ksr__) || defined(__osf__) || defined(__svr4__) || defined(__svr5__) || defined(__APPLE__)
#define PS_USE_CLOBBER_ARGV
#elif defined(WIN32)
#define PS_USE_WIN32
#else
#define PS_USE_NONE
#endif

/* Different systems want the buffer padded differently */
#if defined(_AIX) || defined(__linux__) || defined(__svr4__) || defined(__APPLE__)
#define PS_PADDING '\0'
#else
#define PS_PADDING ' '
#endif

#ifndef PS_USE_CLOBBER_ARGV
/* all but one options need a buffer to write their ps line in */
#define PS_BUFFER_SIZE 256
static char ps_buffer[PS_BUFFER_SIZE];
static const size_t ps_buffer_size = PS_BUFFER_SIZE;
#else /* PS_USE_CLOBBER_ARGV */
static char *ps_buffer;		/* will point to argv area */
static size_t ps_buffer_size;	/* space determined at run time */
static size_t ps_last_status_len;	/* use to minimize length of clobber */
#endif /* PS_USE_CLOBBER_ARGV */
static size_t ps_sentinel_size; /* that many trailing bytes in ps_buffer
                                 * are reserved and must be filled with
                                 * PS_PADDING */

#if defined(PS_USE_CHANGE_ARGV) || defined(PS_USE_CLOBBER_ARGV)
static volatile void *ps_leaks[2]; /* we leak memory, hello valgrind */
#endif

#if defined(PS_USE_CHANGE_ARGV) || defined(PS_USE_CLOBBER_ARGV)
/*
 * A copy of the memory block within clobber_begin, clobber_end
 * was created to preserve its content.
 */
struct ps_relocation
{
	char *clobber_begin;
	char *clobber_end;
	char *copy_begin;
};

/*
 * If an entity is in a clobber area, hand back a pointer to
 * an entity in the copy area (the entity and its copy have the same
 * offset).
 */
static inline void *ps_relocate(
	const struct ps_relocation *rel, void *p)
{
	if (rel && (char *)p >= rel->clobber_begin && (char *)p < rel->clobber_end)
		return rel->copy_begin + ((char *)p - rel->clobber_begin);
	return p;
}

static void
ps_argv_changed(const struct ps_relocation *rel, char **new_argv)
{
	(void)rel;
	(void)new_argv;

#if defined(__GLIBC__)
	program_invocation_name =
		ps_relocate(rel, program_invocation_name);
	program_invocation_short_name =
		ps_relocate(rel, program_invocation_short_name);
#endif

#if defined(HAVE_SETPROGNAME) && defined(HAVE_GETPROGNAME)
	setprogname(ps_relocate(rel, (void *)getprogname()));
#endif

#if defined(__APPLE__)
	/*
	 * Darwin (and perhaps other NeXT-derived platforms?) has a static
	 * copy of the argv pointer, which we may fix like so:
	 */
	*_NSGetArgv() = new_argv;
#endif
}
#endif

#if defined(PS_USE_CLOBBER_ARGV)
static void
ps_expand_clobber_area(struct ps_relocation *rel, int argc, char **argv)
{
	int i;
	for (i = 0; i < argc; i++) {
		if (rel->clobber_begin == NULL) {
			rel->clobber_begin = rel->clobber_end = argv[i];
		}
		if (argv[i] != NULL && rel->clobber_end == argv[i]) {
			rel->clobber_end += strlen(argv[i]) + 1;
		}
	}
}

static void
ps_relocate_argv(struct ps_relocation *rel,
                  int argc, char **argv,
			      char **argv_copy)
{
	int i;
	for (i = 0; i < argc; i++) {
		argv_copy[i] = ps_relocate(rel, argv[i]);
	}
	argv_copy[argc] = NULL;
}
#endif

/*
 * Call this early in startup to save the original argc/argv values.
 * If needed, we make a copy of the original argv[] array to preserve it
 * from being clobbered by subsequent ps_display actions.
 *
 * (The original argv[] will not be overwritten by this routine, but may be
 * overwritten during init_ps_display.	Also, the physical location of the
 * environment strings may be moved, so this should be called before any code
 * that might try to hang onto a getenv() result.)
 */
char **
proc_title_init(int argc, char **argv)
{
	(void)argc;
#if defined(PS_USE_CLOBBER_ARGV)
	struct ps_relocation rel = {NULL, NULL, NULL};
	char **argv_copy, **environ_copy;
	char *mem;
	size_t argv_copy_size, clobber_size;
	int envc = 0;
	while (environ[envc]) {
		envc++;
	}
	argv_copy_size = sizeof(argv[0]) * (argc + 1);
	/*
	 * will be overwriting the memory occupied by argv/environ strings
	 * (clobber area), determine clobber area dimensions
	 */
	ps_expand_clobber_area(&rel, argc, argv);
	ps_expand_clobber_area(&rel, envc, environ);
	clobber_size = rel.clobber_end - rel.clobber_begin;
	/*
	 * one memory block to store both argv_copy and the copy of the
	 * clobber area
	 */
	mem = malloc(argv_copy_size + clobber_size);
	if (mem == NULL) {
		return NULL;
	}
	rel.copy_begin = mem + argv_copy_size;
	memcpy(rel.copy_begin, rel.clobber_begin, clobber_size);
	argv_copy = (void *)mem;
	ps_relocate_argv(&rel, argc, argv, argv_copy);
	/*
	 * environ_copy is allocated separately, this is due to libc calling
	 * realloc on the environ in setenv;
	 * note: do NOT overwrite environ inplace, changing environ pointer
	 * is mandatory to flush internal libc caches on getenv/setenv
	 */
	environ_copy = malloc(sizeof(environ[0]) * (envc + 1));
	if (environ_copy == NULL) {
		free(mem);
		return NULL;
	}
	ps_relocate_argv(&rel, envc, environ, environ_copy);
	ps_argv_changed(&rel, argv_copy);
	ps_buffer = rel.clobber_begin;
	ps_buffer_size = ps_last_status_len = clobber_size;
	ps_leaks[0] = argv = argv_copy;
	ps_leaks[1] = environ = environ_copy;
#ifdef __APPLE__
	/*
	 * http://opensource.apple.com/source/adv_cmds/adv_cmds-158/ps/print.c
	 *
	 * ps on osx fetches command line from a process with {CTL_KERN,
	 * KERN_PROCARGS2, <pid>} sysctl. The call returns cached argc + a
	 * copy of the memory area where argv/environ strings live.
	 *
	 * If initially there were 10 arguments, ps is expecting to find 10
	 * \0 separated strings but we've written the process title on top
	 * of that, so ps will try to find more strings; this can result in
	 * a garbage from environment area showing (which we often fail to
	 * overwrite completely). To fix it we write additional \0
	 * terminators at the end of the title (a 'sentinel').
	 */
	ps_sentinel_size = argc - 1;
#endif
#endif

#if defined(PS_USE_CHANGE_ARGV)
	size_t size = sizeof(argv[0] * (argc + 1));
	char **argv_copy = malloc(size);
	if (argv_copy == NULL) {
		return NULL;
	}
	memcpy(argv_copy, argv, size);
	ps_argv_changed(NULL, argv_copy);
	ps_leaks[0] = argv = argv_copy;
#endif

	return argv;
}

void
proc_title_free(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	/*
	 * Intentionally a noop. Undoing proc_title_init is hard and
	 * unsafe because all sorts of code could have grabbed pointers from
	 * argv/environ by now.
	 */
}

void
proc_title_set(const char *format, ...)
{
#ifndef PS_USE_NONE
	va_list ap;
	int buflen;

#ifdef PS_USE_CLOBBER_ARGV
	/* If ps_buffer is a pointer, it might still be null */
	if (!ps_buffer)
		return;
#endif

	/* Update ps_buffer to contain both fixed part and activity */
	va_start(ap, format);
	buflen = vsnprintf(ps_buffer,
		  ps_buffer_size - ps_sentinel_size, format, ap);
	va_end(ap);

	if (buflen < 0)
		return;

	/* Transmit new setting to kernel, if necessary */
#ifdef PS_USE_SETPROCTITLE
	setproctitle("-%s", ps_buffer);
#endif

#ifdef PS_USE_PSTAT
	{
		union pstun pst;

		pst.pst_command = ps_buffer;
		pstat(PSTAT_SETCMD, pst, strlen(ps_buffer), 0, 0);
	}
#endif /* PS_USE_PSTAT */

#ifdef PS_USE_PS_STRINGS
    static char *argvstr[2];
    argvstr[0] = ps_buffer;
	PS_STRINGS->ps_nargvstr = 1;
	PS_STRINGS->ps_argvstr = argvstr;
#endif /* PS_USE_PS_STRINGS */

#ifdef PS_USE_CLOBBER_ARGV
	{
		/* clobber remainder of old status string */
		if (ps_last_status_len > (size_t)buflen)
			memset(ps_buffer + buflen, PS_PADDING, ps_last_status_len - buflen);
		ps_last_status_len = buflen;
	}
#endif /* PS_USE_CLOBBER_ARGV */

#ifdef PS_USE_WIN32
	{
		/*
		 * Win32 does not support showing any changed arguments. To make it at
		 * all possible to track which backend is doing what, we create a
		 * named object that can be viewed with for example Process Explorer.
		 */
		static HANDLE ident_handle = INVALID_HANDLE_VALUE;
		char name[PS_BUFFER_SIZE + 32];

		if (ident_handle != INVALID_HANDLE_VALUE)
			CloseHandle(ident_handle);

		sprintf(name, "pgident(%d): %s", MyProcPid, ps_buffer);

		ident_handle = CreateEvent(NULL, TRUE, FALSE, name);
	}
#endif /* PS_USE_WIN32 */
#endif /* not PS_USE_NONE */
}

size_t
proc_title_max_length(void)
{
	return ps_buffer_size - ps_sentinel_size;
}
