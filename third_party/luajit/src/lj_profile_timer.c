/*
** Simple profiling timer.
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_profile_timer_c
#define LUA_CORE

#include "lj_profile_timer.h"

#if LJ_HASPROFILE

#if LJ_PROFILE_SIGPROF

/* Start profiling timer. */
void lj_profile_timer_start(lj_profile_timer *timer)
{
  const int interval = timer->opt.interval_msec;
  struct itimerval tm;
  struct sigaction sa;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = interval / 1000;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = (interval % 1000) * 1000;
  setitimer(ITIMER_PROF, &tm, NULL);
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sa.sa_sigaction = timer->opt.handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPROF, &sa, &timer->oldsa);
}

/* Stop profiling timer. */
void lj_profile_timer_stop(lj_profile_timer *timer)
{
  struct itimerval tm;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = 0;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = 0;
  setitimer(ITIMER_PROF, &tm, NULL);
  sigaction(SIGPROF, &timer->oldsa, NULL);
}

#elif LJ_PROFILE_PTHREAD

/* POSIX timer thread. */
static void *timer_thread(lj_profile_timer *timer)
{
  int interval = timer->opt.interval_msec;
#if !LJ_TARGET_PS3
  struct timespec ts;
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
#endif
  while (1) {
#if LJ_TARGET_PS3
    sys_timer_usleep(interval * 1000);
#else
    nanosleep(&ts, NULL);
#endif
    if (timer->abort) break;
    timer->opt.handler();
  }
  return NULL;
}

/* Start profiling timer thread. */
void lj_profile_timer_start(lj_profile_timer *timer)
{
  pthread_mutex_init(&timer->lock, 0);
  timer->abort = 0;
  pthread_create(&timer->thread, NULL, (void *(*)(void *))timer_thread,
		 timer);
}

/* Stop profiling timer thread. */
void lj_profile_timer_stop(lj_profile_timer *timer)
{
  timer->abort = 1;
  pthread_join(timer->thread, NULL);
  pthread_mutex_destroy(&timer->lock);
}

#elif LJ_PROFILE_WTHREAD

/* Windows timer thread. */
static DWORD WINAPI timer_thread(void *timerx)
{
  lj_profile_timer *timer = (lj_profile_timer *)timerx;
  int interval = timer->opt.interval_msec;
#if LJ_TARGET_WINDOWS && !LJ_TARGET_UWP
  timer->wmm_tbp(interval);
#endif
  while (1) {
    Sleep(interval);
    if (timer->abort) break;
    timer->opt.handler();
  }
#if LJ_TARGET_WINDOWS && !LJ_TARGET_UWP
  timer->wmm_tep(interval);
#endif
  return 0;
}

/* Start profiling timer thread. */
void lj_profile_timer_start(lj_profile_timer *timer)
{
#if LJ_TARGET_WINDOWS && !LJ_TARGET_UWP
  if (!timer->wmm) { /* Load WinMM library on-demand. */
    timer->wmm = LJ_WIN_LOADLIBA("winmm.dll");
    if (timer->wmm) {
      timer->wmm_tbp =
	(WMM_TPFUNC)GetProcAddress(timer->wmm, "timeBeginPeriod");
      timer->wmm_tep = (WMM_TPFUNC)GetProcAddress(timer->wmm, "timeEndPeriod");
      if (!timer->wmm_tbp || !timer->wmm_tep) {
	timer->wmm = NULL;
	return;
      }
    }
  }
#endif
  InitializeCriticalSection(&timer->lock);
  timer->abort = 0;
  timer->thread = CreateThread(NULL, 0, timer_thread, timer, 0, NULL);
}

/* Stop profiling timer thread. */
void lj_profile_timer_stop(lj_profile_timer *timer)
{
  timer->abort = 1;
  WaitForSingleObject(timer->thread, INFINITE);
  DeleteCriticalSection(&timer->lock);
}

#endif

#endif  /* LJ_HASPROFILE */
