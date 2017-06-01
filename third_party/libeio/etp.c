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

#ifndef ETP_CB
typedef void (*ETP_CB) (void *);
# define ETP_CB ETP_CB
#endif
#ifndef ETP_WANT_POLL
# define ETP_WANT_POLL(user) user->want_poll_cb (user->userdata)
#endif
#ifndef ETP_DONE_POLL
# define ETP_DONE_POLL(user) user->done_poll_cb (user->userdata)
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
typedef struct etp_pool_user *etp_pool_user;

typedef struct etp_worker
{
  etp_pool pool;

  struct etp_tmpbuf tmpbuf;

#ifdef ETP_WORKER_COMMON
  ETP_WORKER_COMMON
#endif
} etp_worker;

struct etp_pool
{
   etp_reqq req_queue;

   unsigned int started, idle, wanted;

   unsigned int nreqs_run;    /* pool->lock */
   unsigned int max_idle;     /* maximum number of threads that can pool->idle indefinitely */
   unsigned int idle_timeout; /* number of seconds after which an pool->idle threads exit */

   xmutex_t lock;
   xcond_t  reqwait;
   xcond_t  wrkwait;

   int (*on_start_cb)(void *data);
   void *on_start_data;
   int (*on_stop_cb)(void *data);
   void *on_stop_data;
};

struct etp_pool_user
{
   etp_pool pool;

   void *userdata;

   etp_reqq res_queue;

   unsigned int max_poll_time;
   unsigned int max_poll_reqs;

   unsigned int nreqs;

   ETP_CB want_poll_cb;
   ETP_CB done_poll_cb;

   xmutex_t lock;
};

/* worker threads management */

static void ecb_cold
etp_worker_free (etp_worker *wrk)
{
  free (wrk->tmpbuf.ptr);
  free (wrk);
}

ETP_API_DECL unsigned int
etp_nreqs (etp_pool_user user)
{
  return user->nreqs;
}

ETP_API_DECL unsigned int
etp_npending (etp_pool_user user)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (user->lock);
  retval = user->res_queue.size;
  if (WORDACCESS_UNSAFE) X_UNLOCK (user->lock);

  return retval;
}

ETP_API_DECL unsigned int
etp_nthreads (etp_pool pool)
{
  unsigned int retval;

  if (WORDACCESS_UNSAFE) X_LOCK   (pool->lock);
  retval = pool->started;
  if (WORDACCESS_UNSAFE) X_UNLOCK (pool->lock);

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
etp_init (etp_pool pool)
{
  X_MUTEX_CREATE (pool->lock);
  X_COND_CREATE  (pool->reqwait);
  X_COND_CREATE  (pool->wrkwait);

  reqq_init (&pool->req_queue);

  pool->started  = 0;
  pool->idle     = 0;
  pool->wanted   = 4;
  pool->nreqs_run  = 0;

  pool->max_idle = 4;      /* maximum number of threads that can pool->idle indefinitely */
  pool->idle_timeout = 10; /* number of seconds after which an pool->idle threads exit */

  return 0;
}

ETP_API_DECL int ecb_cold
etp_user_init (etp_pool_user user, void *userdata, ETP_CB want_poll, ETP_CB done_poll)
{
  user->pool = NULL;
  X_MUTEX_CREATE (user->lock);

  reqq_init (&user->res_queue);

  user->max_poll_time = 0;
  user->max_poll_reqs = 0;
  user->nreqs    = 0;

  user->userdata     = userdata;
  user->want_poll_cb = want_poll;
  user->done_poll_cb = done_poll;

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
  etp_pool pool = (etp_pool)thr_arg;
  etp_worker self = {};
  self.pool = pool;
  etp_pool_user user; /* per request */

  etp_proc_init ();

  /* try to distribute timeouts somewhat evenly (nanosecond part) */
  ts.tv_nsec = (unsigned long)random() * (1000000000UL / RAND_MAX);

  X_LOCK (pool->lock);

  if (pool->on_start_cb)
    if (pool->on_start_cb(pool->on_start_data))
      goto error;

  for (;;)
    {
      for (;;)
        {
          req = reqq_shift (&pool->req_queue);

          if (ecb_expect_true (req))
            break;

          if (pool->started > pool->wanted) /* someone is shrinking the pool */
            goto quit;

          ++pool->idle;

          if (pool->idle <= pool->max_idle)
            {
              /* we are allowed to pool->idle, so do so without any timeout */
              X_COND_WAIT (pool->reqwait, pool->lock);
              --pool->idle;
            }
          else
            {
              ts.tv_sec = time (0) + pool->idle_timeout;

              if (X_COND_TIMEDWAIT (pool->reqwait, pool->lock, ts) != ETIMEDOUT)
                continue;

              --pool->idle;
              goto quit;
            }
        }

      ++pool->nreqs_run;

      X_UNLOCK (pool->lock);

      user = req->pool_user;
      ETP_EXECUTE (&self, req);

      X_LOCK (user->lock);

      if (!reqq_push (&user->res_queue, req))
        ETP_WANT_POLL (user);

      X_UNLOCK (user->lock);

      X_LOCK (pool->lock);
      --pool->nreqs_run;
    }

quit:
  assert(pool->started > 0);
  pool->started--;
  X_COND_BROADCAST (pool->wrkwait);
  X_UNLOCK (pool->lock);
  if (pool->on_stop_cb)
    pool->on_stop_cb(pool->on_stop_data);

  return 0;

error:
  assert(pool->started > 0);
  pool->started--;
  X_COND_BROADCAST (pool->wrkwait);
  X_UNLOCK (pool->lock);
  return 0;
}

static void ecb_cold
etp_start_thread (etp_pool pool)
{
  xthread_t tid;
  int threads;

  if (xthread_create (&tid, etp_proc, (void *)pool) != 0)
    return;

  X_LOCK (pool->lock);
  assert(pool->started > 0);
  threads = --pool->started;
  X_COND_BROADCAST (pool->wrkwait);
  X_UNLOCK (pool->lock);

  /* Assume if at least one thread managed to start the queue will drain
   * eventually. If not, tasks will never complete; the best we can do
   * is to die now.
   */
  if (threads == 0)
    {
      fputs("failed to start thread in ETP pool", stderr);
      abort();
    }
}

ETP_API_DECL int
etp_poll (etp_pool_user user)
{
  unsigned int maxreqs;
  unsigned int maxtime;
  struct timeval tv_start, tv_now;

  maxreqs = user->max_poll_reqs;
  maxtime = user->max_poll_time;

  if (maxtime)
    gettimeofday (&tv_start, 0);

  for (;;)
    {
      ETP_REQ *req;

      X_LOCK (user->lock);
      req = reqq_shift (&user->res_queue);

      if (ecb_expect_true (req))
        {
          if (ecb_expect_true (user->nreqs))
            --user->nreqs;

          if (!user->res_queue.size)
            ETP_DONE_POLL (user);
        }

      X_UNLOCK (user->lock);

      if (ecb_expect_false (!req))
        return 0;

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
etp_grp_cancel (etp_pool_user user, ETP_REQ *grp);

ETP_API_DECL void
etp_cancel (etp_pool_user user, ETP_REQ *req)
{
  req->cancelled = 1;

  etp_grp_cancel (user, req);
}

ETP_API_DECL void
etp_grp_cancel (etp_pool_user user, ETP_REQ *grp)
{
  for (grp = grp->grp_first; grp; grp = grp->grp_next)
    etp_cancel (user, grp);
}

ETP_API_DECL void
etp_submit (etp_pool_user user, ETP_REQ *req)
{
  req->pri -= ETP_PRI_MIN;

  if (ecb_expect_false (req->pri < ETP_PRI_MIN - ETP_PRI_MIN)) req->pri = ETP_PRI_MIN - ETP_PRI_MIN;
  if (ecb_expect_false (req->pri > ETP_PRI_MAX - ETP_PRI_MIN)) req->pri = ETP_PRI_MAX - ETP_PRI_MIN;

  user->nreqs++;
  if (ecb_expect_false (req->type == ETP_TYPE_GROUP))
    {
      X_LOCK (user->lock);

      if (!reqq_push (&user->res_queue, req))
        ETP_WANT_POLL (user);

      X_UNLOCK (user->lock);
    }
  else
    {
      etp_pool pool = user->pool;
      int need_thread = 0;

      X_LOCK (pool->lock);
      req->pool_user = user;
      reqq_push (&pool->req_queue, req);
      if (ecb_expect_false(pool->req_queue.size + pool->nreqs_run > pool->started &&
                           pool->started < pool->wanted))
        {
          /* arrange for a thread to start */
          need_thread = 1;
          pool->started++;
        }
      X_COND_SIGNAL (pool->reqwait);
      X_UNLOCK (pool->lock);
      if (ecb_expect_false(need_thread))
        etp_start_thread(pool);
    }
}

ETP_API_DECL void ecb_cold
etp_set_max_poll_time (etp_pool_user user, double seconds)
{
  user->max_poll_time = seconds * ETP_TICKS;
}

ETP_API_DECL void ecb_cold
etp_set_max_poll_reqs (etp_pool_user user, unsigned int maxreqs)
{
  user->max_poll_reqs = maxreqs;
}

ETP_API_DECL void ecb_cold
etp_set_thread_on_start(etp_pool pool, int (*on_start_cb)(void *), void *data)
{
  pool->on_start_cb = on_start_cb;
  pool->on_start_data = data;
}

ETP_API_DECL void ecb_cold
etp_set_thread_on_stop(etp_pool pool, int (*on_stop_cb)(void *), void *data)
{
  pool->on_stop_cb = on_stop_cb;
  pool->on_stop_data = data;
}


ETP_API_DECL void ecb_cold
etp_set_max_idle (etp_pool pool, unsigned int threads)
{
  X_LOCK   (pool->lock);
  pool->max_idle = threads;
  X_UNLOCK (pool->lock);
}

ETP_API_DECL void ecb_cold
etp_set_idle_timeout (etp_pool pool, unsigned int seconds)
{
  X_LOCK   (pool->lock);
  pool->idle_timeout = seconds;
  X_UNLOCK (pool->lock);
}

ETP_API_DECL void ecb_cold
etp_set_min_parallel (etp_pool pool, unsigned int threads)
{
  X_LOCK   (pool->lock);
  if (pool->wanted < threads)
    pool->wanted = threads;
  X_UNLOCK (pool->lock);
}

ETP_API_DECL int ecb_cold
etp_set_max_parallel (etp_pool pool, unsigned int threads)
{
  int retval;
  X_LOCK   (pool->lock);
  retval = pool->wanted;
  if (pool->wanted > threads)
    pool->wanted = threads;

  while (pool->started > pool->wanted)
    {
      X_COND_BROADCAST(pool->reqwait); /* wake idle threads */
      X_COND_WAIT(pool->wrkwait, pool->lock);
    }
  X_UNLOCK (pool->lock);
  return retval;
}
