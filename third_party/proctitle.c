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
#if defined(__darwin__)
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
#elif (defined(BSD) || defined(__bsdi__) || defined(__hurd__)) && !defined(__darwin__)
#define PS_USE_CHANGE_ARGV
#elif defined(__linux__) || defined(_AIX) || defined(__sgi) || (defined(sun) && !defined(BSD)) || defined(ultrix) || defined(__ksr__) || defined(__osf__) || defined(__svr4__) || defined(__svr5__) || defined(__darwin__)
#define PS_USE_CLOBBER_ARGV
#elif defined(WIN32)
#define PS_USE_WIN32
#else
#define PS_USE_NONE
#endif

/* Different systems want the buffer padded differently */
#if defined(_AIX) || defined(__linux__) || defined(__svr4__)
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
static size_t last_status_len;	/* use to minimize length of clobber */
#endif /* PS_USE_CLOBBER_ARGV */

static size_t ps_buffer_fixed_size;	/* size of the constant prefix */

/* save the original argv[] location here */
static int save_argc;
static char **save_argv;
/* save the original environ[] here */
static char **save_environ = NULL;

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
init_set_proc_title(int argc, char **argv)
{
	save_argc = argc;
	save_argv = argv;

#if defined(PS_USE_CLOBBER_ARGV)
	save_environ = environ;
	/*
	 * If we're going to overwrite the argv area, count the available space.
	 * Also move the environment to make additional room.
	 */
	{
		char *end_of_area = NULL;
		char **new_environ;
		int i;

		/*
		 * check for contiguous argv strings
		 */
		for (i = 0; i < argc; i++) {
			if (i == 0 || end_of_area + 1 == argv[i])
				end_of_area = argv[i] + strlen(argv[i]);
		}

		if (end_of_area == NULL) {	/* probably can't happen? */
			ps_buffer = NULL;
			ps_buffer_size = 0;
			return argv;
		}

		/*
		 * check for contiguous environ strings following argv
		 */
		for (i = 0; environ[i] != NULL; i++) {
			if (end_of_area + 1 == environ[i])
				end_of_area = environ[i] + strlen(environ[i]);
		}

		ps_buffer = argv[0];
		last_status_len = ps_buffer_size = end_of_area - argv[0];

		/*
		 * move the environment out of the way
		 */
		new_environ = (char **)malloc((i + 1) * sizeof(char *));
		for (i = 0; environ[i] != NULL; i++)
			new_environ[i] = strdup(environ[i]);
		new_environ[i] = NULL;
		environ = new_environ;
	}
#endif /* PS_USE_CLOBBER_ARGV */

#if defined(PS_USE_CHANGE_ARGV) || defined(PS_USE_CLOBBER_ARGV)

	/*
	 * If we're going to change the original argv[] then make a copy for
	 * argument parsing purposes.
	 *
	 * (NB: do NOT think to remove the copying of argv[], even though
	 * postmaster.c finishes looking at argv[] long before we ever consider
	 * changing the ps display.  On some platforms, getopt() keeps pointers
	 * into the argv array, and will get horribly confused when it is
	 * re-called to analyze a subprocess' argument string if the argv storage
	 * has been clobbered meanwhile.  Other platforms have other dependencies
	 * on argv[].
	 */
	{
		char **new_argv;
		int i;

		new_argv = (char **)malloc((argc + 1) * sizeof(char *));
		for (i = 0; i < argc; i++)
			new_argv[i] = strdup(argv[i]);
		new_argv[argc] = NULL;

#if defined(__darwin__)

		/*
		 * Darwin (and perhaps other NeXT-derived platforms?) has a static
		 * copy of the argv pointer, which we may fix like so:
		 */
		*_NSGetArgv() = new_argv;
#endif

		argv = new_argv;
	}
#endif /* PS_USE_CHANGE_ARGV or PS_USE_CLOBBER_ARGV */

#ifndef PS_USE_NONE
	/* init first part of proctitle */

#ifdef PS_USE_SETPROCTITLE

	/*
	 * apparently setproctitle() already adds a `progname:' prefix to the ps
	 * line
	 */
	ps_buffer_fixed_size = 0;
#else
	{
		char basename_buf[PATH_MAX+1];

		/*
		 * At least partially mimic FreeBSD, which for
		 * ./a.out outputs:
		 *
		 * a.out: custom title here (a.out)
	         */
		snprintf(basename_buf, PATH_MAX, "%s", argv[0]);
		snprintf(ps_buffer, ps_buffer_size, "%s: ", basename(basename_buf));
	}

	ps_buffer_fixed_size = strlen(ps_buffer);

#ifdef PS_USE_CLOBBER_ARGV
	if (ps_buffer_size > ps_buffer_fixed_size)
		memset(ps_buffer + ps_buffer_fixed_size, PS_PADDING,
		       ps_buffer_size - ps_buffer_fixed_size);
#endif /* PS_USE_CLOBBER_ARGV */

#endif

#endif /*PS_USE_NONE */

	return argv;
}

void
free_proc_title(int argc, char **argv)
{
	int i;
#if defined(PS_USE_CLOBBER_ARGV)
	for (i = 0; environ[i] != NULL; i++)
		free(environ[i]);
	free(environ);
	environ = save_environ;
#endif /* PS_USE_CLOBBER_ARGV */
#if defined(PS_USE_CHANGE_ARGV) || defined(PS_USE_CLOBBER_ARGV)
	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
#endif /* PS_USE_CHANGE_ARGV or PS_USE_CLOBBER_ARGV */
}

void
set_proc_title(const char *format, ...)
{
	va_list ap;

#ifndef PS_USE_NONE

#ifdef PS_USE_CLOBBER_ARGV
	/* If ps_buffer is a pointer, it might still be null */
	if (!ps_buffer)
		return;
#endif

	/* Update ps_buffer to contain both fixed part and activity */
	va_start(ap, format);
	vsnprintf(ps_buffer + ps_buffer_fixed_size,
		  ps_buffer_size - ps_buffer_fixed_size, format, ap);
	va_end(ap);

	/* Transmit new setting to kernel, if necessary */
#ifdef PS_USE_SETPROCTITLE
	setproctitle("%s", ps_buffer);
#endif

#ifdef PS_USE_PSTAT
	{
		union pstun pst;

		pst.pst_command = ps_buffer;
		pstat(PSTAT_SETCMD, pst, strlen(ps_buffer), 0, 0);
	}
#endif /* PS_USE_PSTAT */

#ifdef PS_USE_PS_STRINGS
	PS_STRINGS->ps_nargvstr = 1;
	PS_STRINGS->ps_argvstr = ps_buffer;
#endif /* PS_USE_PS_STRINGS */

#ifdef PS_USE_CLOBBER_ARGV
	{
		int buflen;

		/* pad unused memory */
		buflen = strlen(ps_buffer);
		/* clobber remainder of old status string */
		if (last_status_len > buflen)
			memset(ps_buffer + buflen, PS_PADDING, last_status_len - buflen);
		last_status_len = buflen;
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
