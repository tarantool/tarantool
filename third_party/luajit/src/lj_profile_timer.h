/*
** Simple profiling timer.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_PROFILE_TIMER_H
#define _LJ_PROFILE_TIMER_H

#include "lj_def.h"
#include "lj_arch.h"

#if LJ_HASPROFILE

#if LJ_PROFILE_SIGPROF

#include <sys/time.h>
#include <signal.h>
#define profile_lock(ps)	UNUSED(ps)
#define profile_unlock(ps)	UNUSED(ps)

#elif LJ_PROFILE_PTHREAD

#include <pthread.h>
#include <time.h>
#if LJ_TARGET_PS3
#include <sys/timer.h>
#endif
#define profile_lock(ps)	pthread_mutex_lock(&ps->timer.lock)
#define profile_unlock(ps)	pthread_mutex_unlock(&ps->timer.lock)

#elif LJ_PROFILE_WTHREAD

#define WIN32_LEAN_AND_MEAN
#if LJ_TARGET_XBOX360
#include <xtl.h>
#include <xbox.h>
#else
#include <windows.h>
#endif
typedef unsigned int (WINAPI *WMM_TPFUNC)(unsigned int);
#define profile_lock(ps)	EnterCriticalSection(&ps->timer.lock)
#define profile_unlock(ps)	LeaveCriticalSection(&ps->timer.lock)

#endif

typedef struct {
#if LJ_PROFILE_SIGPROF
  void (*handler)(int, siginfo_t*, void*);
#else
  void (*handler)(void);
#endif
  uint32_t interval_msec;
} lj_profile_timer_opt;

typedef struct {
  lj_profile_timer_opt opt;
#if LJ_PROFILE_SIGPROF
  struct sigaction oldsa;	/* Previous SIGPROF state. */
#elif LJ_PROFILE_PTHREAD
  pthread_mutex_t lock;		/* g->hookmask update lock. */
  pthread_t thread;		/* Timer thread. */
  int abort;			/* Abort timer thread. */
#elif LJ_PROFILE_WTHREAD
#if LJ_TARGET_WINDOWS
  HINSTANCE wmm;		/* WinMM library handle. */
  WMM_TPFUNC wmm_tbp;		/* WinMM timeBeginPeriod function. */
  WMM_TPFUNC wmm_tep;		/* WinMM timeEndPeriod function. */
#endif
  CRITICAL_SECTION lock;	/* g->hookmask update lock. */
  HANDLE thread;		/* Timer thread. */
  int abort;			/* Abort timer thread. */
#endif
} lj_profile_timer;

/* Start profiling timer. */
void lj_profile_timer_start(lj_profile_timer *timer);

/* Stop profiling timer. */
void lj_profile_timer_stop(lj_profile_timer *timer);

#endif  /* LJ_HASPROFILE */

#endif
