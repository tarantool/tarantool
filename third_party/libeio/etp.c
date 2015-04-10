/*
 * libetp implementation
 *
 * Copyright (c) 2007,2008,2009,2010,2011,2012,2013 Marc Alexander Lehmann <libetp@schmorp.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modifica-
 * tion, are permitted provided that the following conditions are met:
 *
 *   1.  Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *   2.  Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License ("GPL") version 2 or any later version,
 * in which case the provisions of the GPL are applicable instead of
 * the above. If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the BSD license, indicate your decision
 * by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL. If you do not delete the
 * provisions above, a recipient may use your version of this file under
 * either the BSD or the GPL.
 */

#ifndef ETP_API_DECL
# define ETP_API_DECL static
#endif

#ifndef ETP_PRI_MIN
# define ETP_PRI_MIN 0
# define ETP_PRI_MAX 0
#endif

#ifndef ETP_TYPE_QUIT
# define ETP_TYPE_QUIT 0
#endif

#ifndef ETP_TYPE_GROUP
# define ETP_TYPE_GROUP 1
#endif

#define ETP_NUM_PRI (ETP_PRI_MAX - ETP_PRI_MIN + 1)

#define ETP_TICKS ((1000000 + 1023) >> 10)

/* calculate time difference in ~1/ETP_TICKS of a second */
ecb_inline int
etp_tvdiff (struct timeval *tv1, struct timeval *tv2)
{
  return  (tv2->tv_sec  - tv1->tv_sec ) * ETP_TICKS
       + ((tv2->tv_usec - tv1->tv_usec) >> 10);
}

static unsigned int started, idle, wanted = 4;

static void (*want_poll_cb) (void);
static void (*done_poll_cb) (void);
 
static unsigned int max_poll_time;     /* reslock */
static unsigned int max_poll_reqs;     /* reslock */

static unsigned int nreqs;    /* reqlock */
static unsigned int nready;   /* reqlock */
static unsigned int npending; /* reqlock */
static unsigned int max_idle = 4;      /* maximum number of threads that can idle indefinitely */
static unsigned int idle_timeout = 10; /* number of seconds after which an idle threads exit */

static xmutex_t wrklock;
static xmutex_t reslock;
static xmutex_t reqlock;
static xcond_t  reqwait;

typedef struct etp_worker
{
  struct tmpbuf tmpbuf;

  /* locked by wrklock */
  struct etp_worker *prev, *next;

  xthread_t tid;

#ifdef ETP_WORKER_COMMON
  ETP_WORKER_COMMON
#endif
} etp_worker;

static etp_worker wrk_first; /* NOT etp */

#define ETP_WORKER_LOCK(wrk)   X_LOCK   (wrklock)
#define ETP_WORKER_UNLOCK(wrk) X_UNLOCK (wrklock)

/* worker threads management */

static void
etp_worker_clear (etp_worker *wrk)
{
}

static void ecb_cold
etp_worker_free (etp_worker *wrk)
{
  free (wrk->tmpbuf.ptr);

  wrk->next->prev = wrk->prev;
  wrk->prev->next = wrk->next;

  free (wrk);
}

ETP_API_DECL unsigned int
etp_nreqs (void)
{
  int retval;
  if (WORDACCESS_UNSAFE) X_LOCK   (reqlock);
  retval = nreqs;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reqlock);
  return retval;
}

ETP_API_DECL unsigned int
etp_nready (void)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (reqlock);
  retval = nready;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reqlock);

  return retval;
}

ETP_API_DECL unsigned int
etp_npending (void)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (reqlock);
  retval = npending;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reqlock);

  return retval;
}

ETP_API_DECL unsigned int
etp_nthreads (void)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (reqlock);
  retval = started;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reqlock);

  return retval;
}

/*
 * a somewhat faster data structure might be nice, but
 * with 8 priorities this actually needs <20 insns
 * per shift, the most expensive operation.
 */
typedef struct {
  ETP_REQ *qs[ETP_NUM_PRI], *qe[ETP_NUM_PRI]; /* qstart, qend */
  int size;
} etp_reqq;

static etp_reqq req_queue;
static etp_reqq res_queue;

static void ecb_noinline ecb_cold
reqq_init (etp_reqq *q)
{
  int pri;

  for (pri = 0; pri < ETP_NUM_PRI; ++pri)
    q->qs[pri] = q->qe[pri] = 0;

  q->size = 0;
}

static int ecb_noinline
reqq_push (etp_reqq *q, ETP_REQ *req)
{
  int pri = req->pri;
  req->next = 0;

  if (q->qe[pri])
    {
      q->qe[pri]->next = req;
      q->qe[pri] = req;
    }
  else
    q->qe[pri] = q->qs[pri] = req;

  return q->size++;
}

static ETP_REQ * ecb_noinline
reqq_shift (etp_reqq *q)
{
  int pri;

  if (!q->size)
    return 0;

  --q->size;

  for (pri = ETP_NUM_PRI; pri--; )
    {
      ETP_REQ *req = q->qs[pri];

      if (req)
        {
          if (!(q->qs[pri] = (ETP_REQ *)req->next))
            q->qe[pri] = 0;

          return req;
        }
    }

  abort ();
}

ETP_API_DECL int ecb_cold
etp_init (void (*want_poll)(void), void (*done_poll)(void))
{
  X_MUTEX_CREATE (wrklock);
  X_MUTEX_CREATE (reslock);
  X_MUTEX_CREATE (reqlock);
  X_COND_CREATE  (reqwait);

  reqq_init (&req_queue);
  reqq_init (&res_queue);

  wrk_first.next =
  wrk_first.prev = &wrk_first;

  started  = 0;
  idle     = 0;
  nreqs    = 0;
  nready   = 0;
  npending = 0;

  want_poll_cb = want_poll;
  done_poll_cb = done_poll;

  return 0;
}

/* not yet in etp.c */
X_THREAD_PROC (etp_proc);

static void ecb_cold
etp_start_thread (void)
{
  etp_worker *wrk = calloc (1, sizeof (etp_worker));

  /*TODO*/
  assert (("unable to allocate worker thread data", wrk));

  X_LOCK (wrklock);

  if (xthread_create (&wrk->tid, etp_proc, (void *)wrk))
    {
      wrk->prev = &wrk_first;
      wrk->next = wrk_first.next;
      wrk_first.next->prev = wrk;
      wrk_first.next = wrk;
      ++started;
    }
  else
    free (wrk);

  X_UNLOCK (wrklock);
}

static void
etp_maybe_start_thread (void)
{
  if (ecb_expect_true (etp_nthreads () >= wanted))
    return;
  
  /* todo: maybe use idle here, but might be less exact */
  if (ecb_expect_true (0 <= (int)etp_nthreads () + (int)etp_npending () - (int)etp_nreqs ()))
    return;

  etp_start_thread ();
}

static void ecb_cold
etp_end_thread (void)
{
  ETP_REQ *req = calloc (1, sizeof (ETP_REQ)); /* will be freed by worker */

  req->type = ETP_TYPE_QUIT;
  req->pri  = ETP_PRI_MAX - ETP_PRI_MIN;

  X_LOCK (reqlock);
  reqq_push (&req_queue, req);
  X_COND_SIGNAL (reqwait);
  X_UNLOCK (reqlock);

  X_LOCK (wrklock);
  --started;
  X_UNLOCK (wrklock);
}

ETP_API_DECL int
etp_poll (void)
{
  unsigned int maxreqs;
  unsigned int maxtime;
  struct timeval tv_start, tv_now;

  X_LOCK (reslock);
  maxreqs = max_poll_reqs;
  maxtime = max_poll_time;
  X_UNLOCK (reslock);

  if (maxtime)
    gettimeofday (&tv_start, 0);

  for (;;)
    {
      ETP_REQ *req;

      etp_maybe_start_thread ();

      X_LOCK (reslock);
      req = reqq_shift (&res_queue);

      if (req)
        {
          --npending;

          if (!res_queue.size && done_poll_cb)
            done_poll_cb ();
        }

      X_UNLOCK (reslock);

      if (!req)
        return 0;

      X_LOCK (reqlock);
      --nreqs;
      X_UNLOCK (reqlock);

      if (ecb_expect_false (req->type == ETP_TYPE_GROUP && req->size))
        {
          req->int1 = 1; /* mark request as delayed */
          continue;
        }
      else
        {
          int res = ETP_FINISH (req);
          if (ecb_expect_false (res))
            return res;
        }

      if (ecb_expect_false (maxreqs && !--maxreqs))
        break;

      if (maxtime)
        {
          gettimeofday (&tv_now, 0);

          if (etp_tvdiff (&tv_start, &tv_now) >= maxtime)
            break;
        }
    }

  errno = EAGAIN;
  return -1;
}

ETP_API_DECL void
etp_grp_cancel (ETP_REQ *grp);

ETP_API_DECL void
etp_cancel (ETP_REQ *req)
{
  req->cancelled = 1;

  etp_grp_cancel (req);
}

ETP_API_DECL void
etp_grp_cancel (ETP_REQ *grp)
{
  for (grp = grp->grp_first; grp; grp = grp->grp_next)
    etp_cancel (grp);
}

ETP_API_DECL void
etp_submit (ETP_REQ *req)
{
  req->pri -= ETP_PRI_MIN;

  if (ecb_expect_false (req->pri < ETP_PRI_MIN - ETP_PRI_MIN)) req->pri = ETP_PRI_MIN - ETP_PRI_MIN;
  if (ecb_expect_false (req->pri > ETP_PRI_MAX - ETP_PRI_MIN)) req->pri = ETP_PRI_MAX - ETP_PRI_MIN;

  if (ecb_expect_false (req->type == ETP_TYPE_GROUP))
    {
      /* I hope this is worth it :/ */
      X_LOCK (reqlock);
      ++nreqs;
      X_UNLOCK (reqlock);

      X_LOCK (reslock);

      ++npending;

      if (!reqq_push (&res_queue, req) && want_poll_cb)
        want_poll_cb ();

      X_UNLOCK (reslock);
    }
  else
    {
      X_LOCK (reqlock);
      ++nreqs;
      ++nready;
      reqq_push (&req_queue, req);
      X_COND_SIGNAL (reqwait);
      X_UNLOCK (reqlock);

      etp_maybe_start_thread ();
    }
}

ETP_API_DECL void ecb_cold
etp_set_max_poll_time (double nseconds)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (reslock);
  max_poll_time = nseconds * ETP_TICKS;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reslock);
}

ETP_API_DECL void ecb_cold
etp_set_max_poll_reqs (unsigned int maxreqs)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (reslock);
  max_poll_reqs = maxreqs;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reslock);
}

ETP_API_DECL void ecb_cold
etp_set_max_idle (unsigned int nthreads)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (reqlock);
  max_idle = nthreads;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reqlock);
}

ETP_API_DECL void ecb_cold
etp_set_idle_timeout (unsigned int seconds)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (reqlock);
  idle_timeout = seconds;
  if (WORDACCESS_UNSAFE) X_UNLOCK (reqlock);
}

ETP_API_DECL void ecb_cold
etp_set_min_parallel (unsigned int nthreads)
{
  if (wanted < nthreads)
    wanted = nthreads;
}

ETP_API_DECL void ecb_cold
etp_set_max_parallel (unsigned int nthreads)
{
  if (wanted > nthreads)
    wanted = nthreads;

  while (started > wanted)
    etp_end_thread ();
}

