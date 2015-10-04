/*
 * libetp implementation
 *
 * Copyright (c) 2007,2008,2009,2010,2011,2012,2013,2015 Marc Alexander Lehmann <libetp@schmorp.de>
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

#ifndef ETP_WANT_POLL
# define ETP_WANT_POLL(pool) pool->want_poll_cb (pool->userdata)
#endif
#ifndef ETP_DONE_POLL
# define ETP_DONE_POLL(pool) pool->done_poll_cb (pool->userdata)
#endif

#define ETP_NUM_PRI (ETP_PRI_MAX - ETP_PRI_MIN + 1)

#define ETP_TICKS ((1000000 + 1023) >> 10)

enum {
  ETP_FLAG_GROUPADD = 0x04, /* some request was added to the group */
  ETP_FLAG_DELAYED  = 0x08, /* groiup request has been delayed */
};

/* calculate time difference in ~1/ETP_TICKS of a second */
ecb_inline int
etp_tvdiff (struct timeval *tv1, struct timeval *tv2)
{
  return  (tv2->tv_sec  - tv1->tv_sec ) * ETP_TICKS
       + ((tv2->tv_usec - tv1->tv_usec) >> 10);
}

struct etp_tmpbuf
{
  void *ptr;
  int len;
};

static void *
etp_tmpbuf_get (struct etp_tmpbuf *buf, int len)
{
  if (buf->len < len)
    {
      free (buf->ptr);
      buf->ptr = malloc (buf->len = len);
    }

  return buf->ptr;
}

/*
 * a somewhat faster data structure might be nice, but
 * with 8 priorities this actually needs <20 insns
 * per shift, the most expensive operation.
 */
typedef struct
{
  ETP_REQ *qs[ETP_NUM_PRI], *qe[ETP_NUM_PRI]; /* qstart, qend */
  int size;
} etp_reqq;

typedef struct etp_pool *etp_pool;

typedef struct etp_worker
{
  etp_pool pool;

  struct etp_tmpbuf tmpbuf;

  /* locked by pool->wrklock */
  struct etp_worker *prev, *next;

  xthread_t tid;

#ifdef ETP_WORKER_COMMON
  ETP_WORKER_COMMON
#endif
} etp_worker;

struct etp_pool
{
   void *userdata;

   etp_reqq req_queue;
   etp_reqq res_queue;

   unsigned int started, idle, wanted;

   unsigned int max_poll_time;     /* pool->reslock */
   unsigned int max_poll_reqs;     /* pool->reslock */

   unsigned int nreqs;    /* pool->reqlock */
   unsigned int nready;   /* pool->reqlock */
   unsigned int npending; /* pool->reqlock */
   unsigned int max_idle;      /* maximum number of threads that can pool->idle indefinitely */
   unsigned int idle_timeout; /* number of seconds after which an pool->idle threads exit */

   void (*want_poll_cb) (void *userdata);
   void (*done_poll_cb) (void *userdata);
 
   xmutex_t wrklock;
   xmutex_t reslock;
   xmutex_t reqlock;
   xcond_t  reqwait;

   etp_worker wrk_first;
};

#define ETP_WORKER_LOCK(wrk)   X_LOCK   (pool->wrklock)
#define ETP_WORKER_UNLOCK(wrk) X_UNLOCK (pool->wrklock)

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
etp_nreqs (etp_pool pool)
{
  int retval;
  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reqlock);
  retval = pool->nreqs;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reqlock);
  return retval;
}

ETP_API_DECL unsigned int
etp_nready (etp_pool pool)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reqlock);
  retval = pool->nready;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reqlock);

  return retval;
}

ETP_API_DECL unsigned int
etp_npending (etp_pool pool)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reqlock);
  retval = pool->npending;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reqlock);

  return retval;
}

ETP_API_DECL unsigned int
etp_nthreads (etp_pool pool)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reqlock);
  retval = pool->started;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reqlock);

  return retval;
}

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
etp_init (etp_pool pool, void *userdata, void (*want_poll)(void *userdata), void (*done_poll)(void *userdata))
{
  X_MUTEX_CREATE (pool->wrklock);
  X_MUTEX_CREATE (pool->reslock);
  X_MUTEX_CREATE (pool->reqlock);
  X_COND_CREATE  (pool->reqwait);

  reqq_init (&pool->req_queue);
  reqq_init (&pool->res_queue);

  pool->wrk_first.next =
  pool->wrk_first.prev = &pool->wrk_first;

  pool->started  = 0;
  pool->idle     = 0;
  pool->nreqs    = 0;
  pool->nready   = 0;
  pool->npending = 0;
  pool->wanted   = 4;

  pool->max_idle = 4;      /* maximum number of threads that can pool->idle indefinitely */
  pool->idle_timeout = 10; /* number of seconds after which an pool->idle threads exit */

  pool->userdata     = userdata;
  pool->want_poll_cb = want_poll;
  pool->done_poll_cb = done_poll;

  return 0;
}

static void ecb_noinline ecb_cold
etp_proc_init (void)
{
#if HAVE_PRCTL_SET_NAME
  /* provide a more sensible "thread name" */
  char name[16 + 1];
  const int namelen = sizeof (name) - 1;
  int len;

  prctl (PR_GET_NAME, (unsigned long)name, 0, 0, 0);
  name [namelen] = 0;
  len = strlen (name);
  strcpy (name + (len <= namelen - 4 ? len : namelen - 4), "/eio");
  prctl (PR_SET_NAME, (unsigned long)name, 0, 0, 0);
#endif
}

X_THREAD_PROC (etp_proc)
{
  ETP_REQ *req;
  struct timespec ts;
  etp_worker *self = (etp_worker *)thr_arg;
  etp_pool pool = self->pool;

  etp_proc_init ();

  /* try to distribute timeouts somewhat evenly */
  ts.tv_nsec = ((unsigned long)self & 1023UL) * (1000000000UL / 1024UL);

  for (;;)
    {
      ts.tv_sec = 0;

      X_LOCK (pool->reqlock);

      for (;;)
        {
          req = reqq_shift (&pool->req_queue);

          if (ecb_expect_true (req))
            break;

          if (ts.tv_sec == 1) /* no request, but timeout detected, let's quit */
            {
              X_UNLOCK (pool->reqlock);
              X_LOCK (pool->wrklock);
              --pool->started;
              X_UNLOCK (pool->wrklock);
              goto quit;
            }

          ++pool->idle;

          if (pool->idle <= pool->max_idle)
            /* we are allowed to pool->idle, so do so without any timeout */
            X_COND_WAIT (pool->reqwait, pool->reqlock);
          else
            {
              /* initialise timeout once */
              if (!ts.tv_sec)
                ts.tv_sec = time (0) + pool->idle_timeout;

              if (X_COND_TIMEDWAIT (pool->reqwait, pool->reqlock, ts) == ETIMEDOUT)
                ts.tv_sec = 1; /* assuming this is not a value computed above.,.. */
            }

          --pool->idle;
        }

      --pool->nready;

      X_UNLOCK (pool->reqlock);
     
      if (ecb_expect_false (req->type == ETP_TYPE_QUIT))
        goto quit;

      ETP_EXECUTE (self, req);

      X_LOCK (pool->reslock);

      ++pool->npending;

      if (!reqq_push (&pool->res_queue, req))
        ETP_WANT_POLL (pool);

      etp_worker_clear (self);

      X_UNLOCK (pool->reslock);
    }

quit:
  free (req);

  X_LOCK (pool->wrklock);
  etp_worker_free (self);
  X_UNLOCK (pool->wrklock);

  return 0;
}

static void ecb_cold
etp_start_thread (etp_pool pool)
{
  etp_worker *wrk = calloc (1, sizeof (etp_worker));

  /*TODO*/
  assert (("unable to allocate worker thread data", wrk));

  wrk->pool = pool;

  X_LOCK (pool->wrklock);

  if (xthread_create (&wrk->tid, etp_proc, (void *)wrk))
    {
      wrk->prev = &pool->wrk_first;
      wrk->next = pool->wrk_first.next;
      pool->wrk_first.next->prev = wrk;
      pool->wrk_first.next = wrk;
      ++pool->started;
    }
  else
    free (wrk);

  X_UNLOCK (pool->wrklock);
}

static void
etp_maybe_start_thread (etp_pool pool)
{
  if (ecb_expect_true (etp_nthreads (pool) >= pool->wanted))
    return;
  
  /* todo: maybe use pool->idle here, but might be less exact */
  if (ecb_expect_true (0 <= (int)etp_nthreads (pool) + (int)etp_npending (pool) - (int)etp_nreqs (pool)))
    return;

  etp_start_thread (pool);
}

static void ecb_cold
etp_end_thread (etp_pool pool)
{
  ETP_REQ *req = calloc (1, sizeof (ETP_REQ)); /* will be freed by worker */

  req->type = ETP_TYPE_QUIT;
  req->pri  = ETP_PRI_MAX - ETP_PRI_MIN;

  X_LOCK (pool->reqlock);
  reqq_push (&pool->req_queue, req);
  X_COND_SIGNAL (pool->reqwait);
  X_UNLOCK (pool->reqlock);

  X_LOCK (pool->wrklock);
  --pool->started;
  X_UNLOCK (pool->wrklock);
}

ETP_API_DECL int
etp_poll (etp_pool pool)
{
  unsigned int maxreqs;
  unsigned int maxtime;
  struct timeval tv_start, tv_now;

  X_LOCK (pool->reslock);
  maxreqs = pool->max_poll_reqs;
  maxtime = pool->max_poll_time;
  X_UNLOCK (pool->reslock);

  if (maxtime)
    gettimeofday (&tv_start, 0);

  for (;;)
    {
      ETP_REQ *req;

      etp_maybe_start_thread (pool);

      X_LOCK (pool->reslock);
      req = reqq_shift (&pool->res_queue);

      if (ecb_expect_true (req))
        {
          --pool->npending;

          if (!pool->res_queue.size)
            ETP_DONE_POLL (pool);
        }

      X_UNLOCK (pool->reslock);

      if (ecb_expect_false (!req))
        return 0;

      X_LOCK (pool->reqlock);
      --pool->nreqs;
      X_UNLOCK (pool->reqlock);

      if (ecb_expect_false (req->type == ETP_TYPE_GROUP && req->size))
        {
          req->flags |= ETP_FLAG_DELAYED; /* mark request as delayed */
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
etp_grp_cancel (etp_pool pool, ETP_REQ *grp);

ETP_API_DECL void
etp_cancel (etp_pool pool, ETP_REQ *req)
{
  req->cancelled = 1;

  etp_grp_cancel (pool, req);
}

ETP_API_DECL void
etp_grp_cancel (etp_pool pool, ETP_REQ *grp)
{
  for (grp = grp->grp_first; grp; grp = grp->grp_next)
    etp_cancel (pool, grp);
}

ETP_API_DECL void
etp_submit (etp_pool pool, ETP_REQ *req)
{
  req->pri -= ETP_PRI_MIN;

  if (ecb_expect_false (req->pri < ETP_PRI_MIN - ETP_PRI_MIN)) req->pri = ETP_PRI_MIN - ETP_PRI_MIN;
  if (ecb_expect_false (req->pri > ETP_PRI_MAX - ETP_PRI_MIN)) req->pri = ETP_PRI_MAX - ETP_PRI_MIN;

  if (ecb_expect_false (req->type == ETP_TYPE_GROUP))
    {
      /* I hope this is worth it :/ */
      X_LOCK (pool->reqlock);
      ++pool->nreqs;
      X_UNLOCK (pool->reqlock);

      X_LOCK (pool->reslock);

      ++pool->npending;

      if (!reqq_push (&pool->res_queue, req))
        ETP_WANT_POLL (pool);

      X_UNLOCK (pool->reslock);
    }
  else
    {
      X_LOCK (pool->reqlock);
      ++pool->nreqs;
      ++pool->nready;
      reqq_push (&pool->req_queue, req);
      X_COND_SIGNAL (pool->reqwait);
      X_UNLOCK (pool->reqlock);

      etp_maybe_start_thread (pool);
    }
}

ETP_API_DECL void ecb_cold
etp_set_max_poll_time (etp_pool pool, double seconds)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reslock);
  pool->max_poll_time = seconds * ETP_TICKS;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reslock);
}

ETP_API_DECL void ecb_cold
etp_set_max_poll_reqs (etp_pool pool, unsigned int maxreqs)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reslock);
  pool->max_poll_reqs = maxreqs;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reslock);
}

ETP_API_DECL void ecb_cold
etp_set_max_idle (etp_pool pool, unsigned int threads)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reqlock);
  pool->max_idle = threads;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reqlock);
}

ETP_API_DECL void ecb_cold
etp_set_idle_timeout (etp_pool pool, unsigned int seconds)
{
  if (WORDACCESS_UNSAFE) X_LOCK   (pool->reqlock);
  pool->idle_timeout = seconds;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->reqlock);
}

ETP_API_DECL void ecb_cold
etp_set_min_parallel (etp_pool pool, unsigned int threads)
{
  if (pool->wanted < threads)
    pool->wanted = threads;
}

ETP_API_DECL void ecb_cold
etp_set_max_parallel (etp_pool pool, unsigned int threads)
{
  if (pool->wanted > threads)
    pool->wanted = threads;

  while (pool->started > pool->wanted)
    etp_end_thread (pool);
}

