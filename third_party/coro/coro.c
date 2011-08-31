/*
 * Copyright (c) 2001-2011 Marc Alexander Lehmann <schmorp@schmorp.de>
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
 *
 * This library is modelled strictly after Ralf S. Engelschalls article at
 * http://www.gnu.org/software/pth/rse-pmt.ps. So most of the credit must
 * go to Ralf S. Engelschall <rse@engelschall.com>.
 */

#include "coro.h"

#include <string.h>

/*****************************************************************************/
/* ucontext/setjmp/asm backends                                              */
/*****************************************************************************/
#if CORO_UCONTEXT || CORO_SJLJ || CORO_LOSER || CORO_LINUX || CORO_IRIX || CORO_ASM

# if CORO_UCONTEXT
#  include <stddef.h>
# endif

# if !defined(STACK_ADJUST_PTR)
#  if __sgi
/* IRIX is decidedly NON-unix */
#   define STACK_ADJUST_PTR(sp,ss) ((char *)(sp) + (ss) - 8)
#   define STACK_ADJUST_SIZE(sp,ss) ((ss) - 8)
#  elif (__i386__ && CORO_LINUX) || (_M_IX86 && CORO_LOSER)
#   define STACK_ADJUST_PTR(sp,ss) ((char *)(sp) + (ss))
#   define STACK_ADJUST_SIZE(sp,ss) (ss)
#  elif (__amd64__ && CORO_LINUX) || ((_M_AMD64 || _M_IA64) && CORO_LOSER)
#   define STACK_ADJUST_PTR(sp,ss) ((char *)(sp) + (ss) - 8)
#   define STACK_ADJUST_SIZE(sp,ss) (ss)
#  else
#   define STACK_ADJUST_PTR(sp,ss) (sp)
#   define STACK_ADJUST_SIZE(sp,ss) (ss)
#  endif
# endif

# include <stdlib.h>

# if CORO_SJLJ
#  include <stdio.h>
#  include <signal.h>
#  include <unistd.h>
# endif

static coro_func coro_init_func;
static void *coro_init_arg;
static coro_context *new_coro, *create_coro;

static void
coro_init (void)
{
  volatile coro_func func = coro_init_func;
  volatile void *arg = coro_init_arg;

  coro_transfer (new_coro, create_coro);

#if __GCC_HAVE_DWARF2_CFI_ASM && __amd64
  asm (".cfi_undefined rip");
#endif

  func ((void *)arg);

  /* the new coro returned. bad. just abort() for now */
  abort ();
}

# if CORO_SJLJ

static volatile int trampoline_done;

/* trampoline signal handler */
static void
trampoline (int sig)
{
  if (coro_setjmp (new_coro->env))
    coro_init (); /* start it */
  else
    trampoline_done = 1;
}

# endif

# if CORO_ASM

  #if _WIN32
    #define CORO_WIN_TIB 1
  #endif

  asm (
       "\t.text\n"
       "\t.globl coro_transfer\n"
       "coro_transfer:\n"
       /* windows, of course, gives a shit on the amd64 ABI and uses different registers */
       /* http://blogs.msdn.com/freik/archive/2005/03/17/398200.aspx */
       #if __amd64
         #ifdef WIN32
           /* TODO: xmm6..15 also would need to be saved. sigh. */
           #define NUM_SAVED 8
           "\tpushq %rsi\n"
           "\tpushq %rdi\n"
           "\tpushq %rbp\n"
           "\tpushq %rbx\n"
           "\tpushq %r12\n"
           "\tpushq %r13\n"
           "\tpushq %r14\n"
           "\tpushq %r15\n"
           #if CORO_WIN_TIB
             "\tpushq %fs:0x0\n"
             "\tpushq %fs:0x8\n"
             "\tpushq %fs:0xc\n"
           #endif
           "\tmovq %rsp, (%rcx)\n"
           "\tmovq (%rdx), %rsp\n"
           #if CORO_WIN_TIB
             "\tpopq %fs:0xc\n"
             "\tpopq %fs:0x8\n"
             "\tpopq %fs:0x0\n"
           #endif
           "\tpopq %r15\n"
           "\tpopq %r14\n"
           "\tpopq %r13\n"
           "\tpopq %r12\n"
           "\tpopq %rbx\n"
           "\tpopq %rbp\n"
           "\tpopq %rdi\n"
           "\tpopq %rsi\n"
         #else
           #define NUM_SAVED 6
           "\tpushq %rbp\n"
           "\tpushq %rbx\n"
           "\tpushq %r12\n"
           "\tpushq %r13\n"
           "\tpushq %r14\n"
           "\tpushq %r15\n"
           "\tmovq %rsp, (%rdi)\n"
           "\tmovq (%rsi), %rsp\n"
           "\tpopq %r15\n"
           "\tpopq %r14\n"
           "\tpopq %r13\n"
           "\tpopq %r12\n"
           "\tpopq %rbx\n"
           "\tpopq %rbp\n"
         #endif
       #elif __i386
         #define NUM_SAVED 4
         "\tpushl %ebp\n"
         "\tpushl %ebx\n"
         "\tpushl %esi\n"
         "\tpushl %edi\n"
         #if CORO_WIN_TIB
           "\tpushl %fs:0\n"
           "\tpushl %fs:4\n"
           "\tpushl %fs:8\n"
         #endif
         "\tmovl %esp, (%eax)\n"
         "\tmovl (%edx), %esp\n"
         #if CORO_WIN_TIB
           "\tpopl %fs:8\n"
           "\tpopl %fs:4\n"
           "\tpopl %fs:0\n"
         #endif
         "\tpopl %edi\n"
         "\tpopl %esi\n"
         "\tpopl %ebx\n"
         "\tpopl %ebp\n"
       #else
         #error unsupported architecture
       #endif
       "\tret\n"
  );

# endif

void
coro_create (coro_context *ctx, coro_func coro, void *arg, void *sptr, long ssize)
{
  coro_context nctx;
# if CORO_SJLJ
  stack_t ostk, nstk;
  struct sigaction osa, nsa;
  sigset_t nsig, osig;
# endif

  if (!coro)
    return;

  coro_init_func = coro;
  coro_init_arg  = arg;

  new_coro    = ctx;
  create_coro = &nctx;

# if CORO_SJLJ
  /* we use SIGUSR2. first block it, then fiddle with it. */

  sigemptyset (&nsig);
  sigaddset (&nsig, SIGUSR2);
  sigprocmask (SIG_BLOCK, &nsig, &osig);

  nsa.sa_handler = trampoline;
  sigemptyset (&nsa.sa_mask);
  nsa.sa_flags = SA_ONSTACK;

  if (sigaction (SIGUSR2, &nsa, &osa))
    {
      perror ("sigaction");
      abort ();
    }

  /* set the new stack */
  nstk.ss_sp    = STACK_ADJUST_PTR (sptr, ssize); /* yes, some platforms (IRIX) get this wrong. */
  nstk.ss_size  = STACK_ADJUST_SIZE (sptr, ssize);
  nstk.ss_flags = 0;

  if (sigaltstack (&nstk, &ostk) < 0)
    {
      perror ("sigaltstack");
      abort ();
    }

  trampoline_done = 0;
  kill (getpid (), SIGUSR2);
  sigfillset (&nsig); sigdelset (&nsig, SIGUSR2);

  while (!trampoline_done)
    sigsuspend (&nsig);

  sigaltstack (0, &nstk);
  nstk.ss_flags = SS_DISABLE;
  if (sigaltstack (&nstk, 0) < 0)
    perror ("sigaltstack");

  sigaltstack (0, &nstk);
  if (~nstk.ss_flags & SS_DISABLE)
    abort ();

  if (~ostk.ss_flags & SS_DISABLE)
    sigaltstack (&ostk, 0);

  sigaction (SIGUSR2, &osa, 0);
  sigprocmask (SIG_SETMASK, &osig, 0);

# elif CORO_LOSER

  coro_setjmp (ctx->env);
  #if __CYGWIN__ && __i386
    ctx->env[8]                        = (long)    coro_init;
    ctx->env[7]                        = (long)    ((char *)sptr + ssize)         - sizeof (long);
  #elif __CYGWIN__ && __x86_64
    ctx->env[7]                        = (long)    coro_init;
    ctx->env[6]                        = (long)    ((char *)sptr + ssize)         - sizeof (long);
  #elif defined(__MINGW32__)
    ctx->env[5]                        = (long)    coro_init;
    ctx->env[4]                        = (long)    ((char *)sptr + ssize)         - sizeof (long);
  #elif defined(_M_IX86)
    ((_JUMP_BUFFER *)&ctx->env)->Eip   = (long)    coro_init;
    ((_JUMP_BUFFER *)&ctx->env)->Esp   = (long)    STACK_ADJUST_PTR (sptr, ssize) - sizeof (long);
  #elif defined(_M_AMD64)
    ((_JUMP_BUFFER *)&ctx->env)->Rip   = (__int64) coro_init;
    ((_JUMP_BUFFER *)&ctx->env)->Rsp   = (__int64) STACK_ADJUST_PTR (sptr, ssize) - sizeof (__int64);
  #elif defined(_M_IA64)
    ((_JUMP_BUFFER *)&ctx->env)->StIIP = (__int64) coro_init;
    ((_JUMP_BUFFER *)&ctx->env)->IntSp = (__int64) STACK_ADJUST_PTR (sptr, ssize) - sizeof (__int64);
  #else
    #error "microsoft libc or architecture not supported"
  #endif

# elif CORO_LINUX

  coro_setjmp (ctx->env);
  #if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 0 && defined (JB_PC) && defined (JB_SP)
    ctx->env[0].__jmpbuf[JB_PC]        = (long)    coro_init;
    ctx->env[0].__jmpbuf[JB_SP]        = (long)    STACK_ADJUST_PTR (sptr, ssize) - sizeof (long);
  #elif __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 0 && defined (__mc68000__)
    ctx->env[0].__jmpbuf[0].__aregs[0] = (long int)coro_init;
    ctx->env[0].__jmpbuf[0].__sp       = (int *)   ((char *)sptr + ssize)         - sizeof (long);
  #elif defined (__GNU_LIBRARY__) && defined (__i386__)
    ctx->env[0].__jmpbuf[0].__pc       = (char *)  coro_init;
    ctx->env[0].__jmpbuf[0].__sp       = (void *)  ((char *)sptr + ssize)         - sizeof (long);
  #elif defined (__GNU_LIBRARY__) && defined (__amd64__)
    ctx->env[0].__jmpbuf[JB_PC]        = (long)    coro_init;
    ctx->env[0].__jmpbuf[0].__sp       = (void *)  ((char *)sptr + ssize)         - sizeof (long);
  #else
    #error "linux libc or architecture not supported"
  #endif

# elif CORO_IRIX

  coro_setjmp (ctx->env, 0);
  ctx->env[JB_PC]                      = (__uint64_t)coro_init;
  ctx->env[JB_SP]                      = (__uint64_t)STACK_ADJUST_PTR (sptr, ssize) - sizeof (long);

# elif CORO_ASM

  ctx->sp = (void **)(ssize + (char *)sptr);
  *--ctx->sp = (void *)abort; /* needed for alignment only */
  *--ctx->sp = (void *)coro_init;

  #if CORO_WIN_TIB
  *--ctx->sp = 0;                    /* ExceptionList */
  *--ctx->sp = (char *)sptr + ssize; /* StackBase */
  *--ctx->sp = sptr;                 /* StackLimit */
  #endif

  ctx->sp -= NUM_SAVED;
  memset (ctx->sp, 0, sizeof (*ctx->sp) * NUM_SAVED);

# elif CORO_UCONTEXT

  getcontext (&(ctx->uc));

  ctx->uc.uc_link           =  0;
  ctx->uc.uc_stack.ss_sp    = sptr;
  ctx->uc.uc_stack.ss_size  = (size_t)ssize;
  ctx->uc.uc_stack.ss_flags = 0;

  makecontext (&(ctx->uc), (void (*)())coro_init, 0);

# endif

  coro_transfer (create_coro, new_coro);
}

/*****************************************************************************/
/* pthread backend                                                           */
/*****************************************************************************/
#elif CORO_PTHREAD

/* this mutex will be locked by the running coroutine */
pthread_mutex_t coro_mutex = PTHREAD_MUTEX_INITIALIZER;

struct coro_init_args
{
  coro_func func;
  void *arg;
  coro_context *self, *main;
};

static pthread_t null_tid;

/* I'd so love to cast pthread_mutex_unlock to void (*)(void *)... */
static void
mutex_unlock_wrapper (void *arg)
{
  pthread_mutex_unlock ((pthread_mutex_t *)arg);
}

static void *
coro_init (void *args_)
{
  struct coro_init_args *args = (struct coro_init_args *)args_;
  coro_func func = args->func;
  void *arg = args->arg;

  pthread_mutex_lock (&coro_mutex);

  /* we try to be good citizens and use deferred cancellation and cleanup handlers */
  pthread_cleanup_push (mutex_unlock_wrapper, &coro_mutex);
    coro_transfer (args->self, args->main);
    func (arg);
  pthread_cleanup_pop (1);

  return 0;
}

void
coro_transfer (coro_context *prev, coro_context *next)
{
  pthread_cond_signal (&next->cv);
  pthread_cond_wait (&prev->cv, &coro_mutex);
#if __FreeBSD__ /* freebsd is of course broken and needs manual testcancel calls... yay... */
  pthread_testcancel ();
#endif
}

void
coro_create (coro_context *ctx, coro_func coro, void *arg, void *sptr, long ssize)
{
  static coro_context nctx;
  static int once;

  if (!once)
    {
      once = 1;

      pthread_mutex_lock (&coro_mutex);
      pthread_cond_init (&nctx.cv, 0);
      null_tid = pthread_self ();
    }

  pthread_cond_init (&ctx->cv, 0);

  if (coro)
    {
      pthread_attr_t attr;
      struct coro_init_args args;

      args.func = coro;
      args.arg  = arg;
      args.self = ctx;
      args.main = &nctx;

      pthread_attr_init (&attr);
#if __UCLIBC__
      /* exists, but is borked */
      /*pthread_attr_setstacksize (&attr, (size_t)ssize);*/
#elif __CYGWIN__
      /* POSIX, not here */
      pthread_attr_setstacksize (&attr, (size_t)ssize);
#else
      pthread_attr_setstack (&attr, sptr, (size_t)ssize);
#endif
      pthread_attr_setscope (&attr, PTHREAD_SCOPE_PROCESS);
      pthread_create (&ctx->id, &attr, coro_init, &args);

      coro_transfer (args.main, args.self);
    }
  else
    ctx->id = null_tid;
}

void
coro_destroy (coro_context *ctx)
{
  if (!pthread_equal (ctx->id, null_tid))
    {
      pthread_cancel (ctx->id);
      pthread_mutex_unlock (&coro_mutex);
      pthread_join (ctx->id, 0);
      pthread_mutex_lock (&coro_mutex);
    }

  pthread_cond_destroy (&ctx->cv);
}

#else
# error unsupported backend
#endif

