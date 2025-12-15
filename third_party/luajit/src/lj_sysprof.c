/*
** Implementation of sysprof - platform and Lua profiler.
*/

#define lj_sysprof_c
#define LUA_CORE

#include "lj_arch.h"
#include "lj_sysprof.h"

#if LJ_HASSYSPROF

#include "lj_obj.h"
#include "lj_debug.h"
#include "lj_dispatch.h"
#include "lj_frame.h"

#if LJ_HASJIT
#include "lj_jit.h"
#include "lj_trace.h"
#endif

#include "lj_wbuf.h"
#include "lj_profile_timer.h"
#include "lj_symtab.h"

#include <pthread.h>
#include <errno.h>
#include <execinfo.h>

/*
** Number of profiler frames we need to omit during stack
** unwinding.
**   +------------------------+
** 0 | default_backtrace_host |
**   +------------------------+
** 1 | stream_backtrace_host  |
**   +------------------------+
** 2 |  stream_{guest/host}   |
**   +------------------------+
** 3 |      stream_event      |
**   +------------------------+
** 4 | sysprof_record_sample  |
**   +------------------------+
** 5 | sysprof_signal_handler |
**   +------------------------+
*/
#define SYSPROF_HANDLER_STACK_DEPTH 6
#define SYSPROF_BACKTRACE_FRAME_MAX 512

/* Check that vmstate fits in 4 bits (see streaming format) */
#define vmstfit4(st) ((st & ~(uint32_t)((1 << 4) - 1)) == 0)

enum sysprof_state {
  /* Profiler is not running. */
  SPS_IDLE,
  /* Profiler is running. */
  SPS_PROFILE,
  /*
  ** Stopped in case of stopped or failed stream.
  ** Saved errno is set at luaM_sysprof_stop.
  */
  SPS_HALT
};

struct sysprof {
  global_State *g; /* Profiled VM. */
  pthread_t thread; /* Profiled thread. */
  volatile sig_atomic_t state; /* Internal state. */
  struct lj_wbuf out; /* Output accumulator. */
  struct luam_Sysprof_Counters counters; /* Profiling counters. */
  struct luam_Sysprof_Options opt; /* Profiling options. */
  luam_Sysprof_writer writer; /* Writer function for profile events. */
  luam_Sysprof_on_stop on_stop; /* Callback on profiling stopping. */
  luam_Sysprof_backtracer backtracer; /* Backtracing function for the host stack. */
  lj_profile_timer timer; /* Profiling timer. */
  int saved_errno; /* Saved errno when profiler failed. */
};
/*
** XXX: Only one VM can be profiled at a time.
*/

static struct sysprof sysprof = {0};

/* --- Stream ------------------------------------------------------------- */

static const uint8_t ljp_header[] = {'l', 'j', 'p', LJP_FORMAT_VERSION,
                                      0x0, 0x0, 0x0};

static int stream_is_needed(struct sysprof *sp)
{
  return sp->opt.mode != LUAM_SYSPROF_DEFAULT;
}

static int is_unconfigured(struct sysprof *sp)
{
  return sp->backtracer == NULL || sp->on_stop == NULL || sp->writer == NULL;
}

static void stream_prologue(struct sysprof *sp)
{
  /*
  ** XXX: Must be zero for the symtab module to dump all loaded libraries.
  */
  uint32_t unused_lib_adds = 0;
  lj_symtab_dump(&sp->out, sp->g, &unused_lib_adds);
  lj_wbuf_addn(&sp->out, ljp_header, sizeof(ljp_header));
}

static void stream_epilogue(struct sysprof *sp)
{
  lj_wbuf_addbyte(&sp->out, LJP_EPILOGUE_BYTE);
}

static void stream_ffunc_impl(struct lj_wbuf *buf, uint8_t ffid)
{
  lj_wbuf_addbyte(buf, LJP_FRAME_FFUNC);
  lj_wbuf_addu64(buf, ffid);
}

static void stream_lfunc(struct lj_wbuf *buf, const GCfunc *func)
{
  lj_assertX(isluafunc(func), "bad lua function in sysprof stream");
  const GCproto *pt = funcproto(func);
  lj_assertX(pt != NULL, "bad lua function prototype in sysprof stream");
  lj_wbuf_addbyte(buf, LJP_FRAME_LFUNC);
  lj_wbuf_addu64(buf, (uintptr_t)pt);
  lj_wbuf_addu64(buf, (uint64_t)pt->firstline);
}

static void stream_cfunc(struct lj_wbuf *buf, const GCfunc *func)
{
  lj_assertX(iscfunc(func), "bad C function in sysprof stream");
  lj_wbuf_addbyte(buf, LJP_FRAME_CFUNC);
  lj_wbuf_addu64(buf, (uintptr_t)func->c.f);
}

static void stream_ffunc(struct lj_wbuf *buf, const GCfunc *func)
{
  lj_assertX(isffunc(func), "bad fast function in sysprof stream");
  stream_ffunc_impl(buf, func->c.ffid);
}

static void stream_frame_lua(struct lj_wbuf *buf, const cTValue *frame)
{
  const GCfunc *func = frame_func(frame);
  lj_assertX(func != NULL, "bad function in sysprof stream");
  if (isluafunc(func))
    stream_lfunc(buf, func);
  else if (isffunc(func))
    stream_ffunc(buf, func);
  else if (iscfunc(func))
    stream_cfunc(buf, func);
  else
    /* Unreachable. */
    lj_assertX(0, "bad function type in sysprof stream");
}

static void stream_backtrace_lua(struct sysprof *sp, uint32_t vmstate)
{
  global_State *g = sp->g;
  struct lj_wbuf *buf = &sp->out;
  cTValue *top_frame = NULL, *frame = NULL, *bot = NULL;
  lua_State *L = NULL;

  lj_assertX(g != NULL, "uninitialized global state in sysprof state");
  L = gco2th(gcref(g->cur_L));
  lj_assertG(L != NULL, "uninitialized Lua state in sysprof state");
  /*
  ** Lua stack may be inconsistent during the execution of a
  ** fast-function, so instead of updating the `top_frame` for
  ** it, its `ffid` is set instead. The first frame on the
  ** result stack is streamed manually, and the rest of the
  ** stack is streamed based on the previous `top_frame` value.
  */
  if (vmstate == LJ_VMST_FFUNC) {
    uint8_t ffid = g->top_frame_info.ffid;
    stream_ffunc_impl(buf, ffid);
  }

  top_frame = g->top_frame_info.top_frame - 1;

  bot = tvref(L->stack) + LJ_FR2;
  /* Traverse frames backwards */
  for (frame = top_frame; frame > bot; frame = frame_prev(frame)) {
    if (frame_gc(frame) == obj2gco(L) || frame_isvarg(frame))
      continue;  /* Skip dummy frames. See lj_err_optype_call(). */
    stream_frame_lua(buf, frame);
  }

  lj_wbuf_addbyte(buf, LJP_FRAME_LUA_LAST);
}

static void *stream_frame_host(int frame_no, void *addr)
{
  struct sysprof *sp = &sysprof;
  /*
  ** We don't want the profiler stack to be streamed, as it will
  ** burden the profile with unnecessary information.
  */
  if (LJ_UNLIKELY(frame_no <= SYSPROF_HANDLER_STACK_DEPTH))
    return addr;
  else if (LJ_UNLIKELY(sp->opt.mode == LUAM_SYSPROF_LEAF &&
                         frame_no > SYSPROF_HANDLER_STACK_DEPTH))
    return NULL;

  lj_wbuf_addu64(&sp->out, (uintptr_t)addr);
  return addr;
}

static void default_backtrace_host(void *(writer)(int frame_no, void *addr))
{
  static void *backtrace_buf[SYSPROF_BACKTRACE_FRAME_MAX] = {};

  struct sysprof *sp = &sysprof;
  int max_depth = sp->opt.mode == LUAM_SYSPROF_LEAF
                  ? SYSPROF_HANDLER_STACK_DEPTH + 1
                  : SYSPROF_BACKTRACE_FRAME_MAX;
  const int depth = backtrace(backtrace_buf, max_depth);
  int level;

  lj_assertX(depth <= max_depth, "depth of C stack is too big");
  for (level = SYSPROF_HANDLER_STACK_DEPTH; level < depth; ++level) {
    if (!writer(level - SYSPROF_HANDLER_STACK_DEPTH + 1, backtrace_buf[level]))
      return;
  }
}

static void stream_backtrace_host(struct sysprof *sp)
{
  lj_assertX(sp->backtracer != NULL, "uninitialized sysprof backtracer");
  sp->backtracer(stream_frame_host);
  lj_wbuf_addu64(&sp->out, (uintptr_t)LJP_FRAME_HOST_LAST);
}

#if LJ_HASJIT
static void stream_trace(struct sysprof *sp, uint32_t vmstate)
{
  lj_wbuf_addbyte(&sp->out, (uint8_t)vmstate);
  struct lj_wbuf *out = &sp->out;
  uint32_t traceno = sp->g->vmstate;
  jit_State *J = G2J(sp->g);
  GCtrace *trace = traceref(J, traceno);

  GCproto *startpt = gco2pt(gcref(trace->startpt));

  lj_wbuf_addu64(out, traceno);
  lj_wbuf_addu64(out, (uintptr_t)startpt);
  lj_wbuf_addu64(out, startpt->firstline);
}
#endif

static void stream_guest(struct sysprof *sp, uint32_t vmstate)
{
  lj_wbuf_addbyte(&sp->out, (uint8_t)vmstate);
  stream_backtrace_lua(sp, vmstate);
  stream_backtrace_host(sp);
}

static void stream_host(struct sysprof *sp, uint32_t vmstate)
{
  lj_wbuf_addbyte(&sp->out, (uint8_t)vmstate);
  stream_backtrace_host(sp);
}

typedef void (*event_streamer)(struct sysprof *sp, uint32_t vmstate);

static event_streamer event_streamers[] = {
  /* XXX: order is important */
  stream_host,  /* LJ_VMST_INTERP */
  stream_guest, /* LJ_VMST_LFUNC */
  stream_guest, /* LJ_VMST_FFUNC */
  stream_guest, /* LJ_VMST_CFUNC */
  stream_host,  /* LJ_VMST_GC */
  stream_host,  /* LJ_VMST_EXIT */
  stream_host,  /* LJ_VMST_RECORD */
  stream_host,  /* LJ_VMST_OPT */
  stream_host,  /* LJ_VMST_ASM */
#if LJ_HASJIT
  stream_trace  /* LJ_VMST_TRACE */
#endif
};

static void stream_event(struct sysprof *sp, uint32_t vmstate)
{
  event_streamer stream = NULL;

  lj_assertX(vmstfit4(vmstate), "vmstate don't fit in 4 bits");
  stream = event_streamers[vmstate];
  lj_assertX(stream != NULL, "uninitialized sysprof stream");
  stream(sp, vmstate);
}

/* -- Signal handler ------------------------------------------------------ */

static void sysprof_record_sample(struct sysprof *sp, siginfo_t *info)
{
  global_State *g = sp->g;
  uint32_t _vmstate = ~(uint32_t)(g->vmstate);
  uint32_t vmstate;

  /* `g->vmstate` is 0 outside the VM. Hence, dump only the host stack. */
  if (g->vmstate == 0)
    vmstate = LJ_VMST_INTERP;
  else if (_vmstate < LJ_VMST_TRACE)
    vmstate = _vmstate;
  else
    vmstate = LJ_VMST_TRACE;

  lj_assertX(pthread_self() == sp->thread,
	     "bad thread during sysprof record sample");

  /* Caveat: order of counters must match vmstate order in <lj_obj.h>. */
  ((uint64_t *)&sp->counters)[vmstate]++;

  sp->counters.samples++;

  if (!stream_is_needed(sp))
    return;

  stream_event(sp, vmstate);
  if (LJ_UNLIKELY(lj_wbuf_test_flag(&sp->out, STREAM_ERRIO|STREAM_STOP))) {
    sp->saved_errno = lj_wbuf_errno(&sp->out);
    lj_wbuf_terminate(&sp->out);
    sp->state = SPS_HALT;
  }
}

static void sysprof_signal_handler(int sig, siginfo_t *info, void *ctx)
{
  struct sysprof *sp = &sysprof;
  UNUSED(sig);
  UNUSED(ctx);

  switch (sp->state) {
    case SPS_PROFILE:
      sysprof_record_sample(sp, info);
      break;

    case SPS_IDLE:
    case SPS_HALT:
      /* noop */
      break;

    default:
      lj_assertX(0, "bad sysprof profiler state");
      break;
  }
}

/* -- Internal ------------------------------------------------------------ */

static int sysprof_validate(struct sysprof *sp,
                            const struct luam_Sysprof_Options *opt)
{
  switch (sp->state) {
    case SPS_IDLE:
      if (opt->mode > LUAM_SYSPROF_CALLGRAPH) {
        return PROFILE_ERRUSE;
      } else if (opt->mode != LUAM_SYSPROF_DEFAULT &&
                 (opt->buf == NULL || opt->len == 0 || is_unconfigured(sp))) {
        return PROFILE_ERRUSE;
      } else if (opt->interval == 0) {
        return PROFILE_ERRUSE;
      }
      break;

    case SPS_PROFILE:
    case SPS_HALT:
      return PROFILE_ERRRUN;

    default:
      lj_assertX(0, "bad sysprof profiler state");
      break;
  }

  return PROFILE_SUCCESS;
}

static int sysprof_init(struct sysprof *sp, lua_State *L,
                        const struct luam_Sysprof_Options *opt)
{
  const int status = sysprof_validate(sp, opt);
  if (PROFILE_SUCCESS != status)
    return status;

  /* Copy validated options to sysprof state. */
  memcpy(&sp->opt, opt, sizeof(sp->opt));

  /* Init general fields. */
  sp->g = G(L);
  sp->thread = pthread_self();

  /* Reset counters. */
  memset(&sp->counters, 0, sizeof(sp->counters));

  /* Reset saved errno. */
  sp->saved_errno = 0;

  if (stream_is_needed(sp))
    lj_wbuf_init(&sp->out, sp->writer, opt->ctx, opt->buf, opt->len);

  return PROFILE_SUCCESS;
}

/* -- Public profiling API ------------------------------------------------ */

int lj_sysprof_set_writer(luam_Sysprof_writer writer) {
  struct sysprof *sp = &sysprof;

  if (sp->state != SPS_IDLE || writer == NULL)
    return PROFILE_ERRUSE;

  sp->writer = writer;
  if (!is_unconfigured(sp)) {
    sp->state = SPS_IDLE;
  }
  return PROFILE_SUCCESS;
}

int lj_sysprof_set_on_stop(luam_Sysprof_on_stop on_stop) {
  struct sysprof *sp = &sysprof;

  if (sp->state != SPS_IDLE || on_stop == NULL)
    return PROFILE_ERRUSE;

  sp->on_stop = on_stop;
  if (!is_unconfigured(sp)) {
    sp->state = SPS_IDLE;
  }
  return PROFILE_SUCCESS;
}

int lj_sysprof_set_backtracer(luam_Sysprof_backtracer backtracer) {
  struct sysprof *sp = &sysprof;

  if (sp->state != SPS_IDLE)
    return PROFILE_ERRUSE;
  if (backtracer == NULL) {
    sp->backtracer = default_backtrace_host;
    /*
    ** XXX: `backtrace` is not signal-safe, according to man,
    ** because it is lazy loaded on the first call, which triggers
    ** allocations. We need to call `backtrace` before starting profiling
    ** to avoid lazy loading.
    */
    void *dummy = NULL;
    backtrace(&dummy, 1);
  }
  else {
    sp->backtracer = backtracer;
  }
  if (!is_unconfigured(sp)) {
    sp->state = SPS_IDLE;
  }
  return PROFILE_SUCCESS;
}

int lj_sysprof_start(lua_State *L, const struct luam_Sysprof_Options *opt)
{
  struct sysprof *sp = &sysprof;

  int status = sysprof_init(sp, L, opt);
  if (PROFILE_SUCCESS != status) {
    if (NULL != sp->on_stop) {
      /*
      ** Initialization may fail in case of unconfigured sysprof,
      ** so we cannot guarantee cleaning up resources in this case.
      */
      sp->on_stop(opt->ctx, opt->buf);
    }
    return status;
  }

  sp->state = SPS_PROFILE;

  if (stream_is_needed(sp)) {
    stream_prologue(sp);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(&sp->out, STREAM_ERRIO|STREAM_STOP))) {
      /* on_stop call may change errno value. */
      const int saved_errno = lj_wbuf_errno(&sp->out);
      /* Ignore possible errors. mp->out.buf may be NULL here. */
      sp->on_stop(opt->ctx, sp->out.buf);
      lj_wbuf_terminate(&sp->out);
      sp->state = SPS_IDLE;
      errno = saved_errno;
      return PROFILE_ERRIO;
    }
  }

  sp->timer.opt.interval_msec = opt->interval;
  sp->timer.opt.handler = sysprof_signal_handler;
  lj_profile_timer_start(&sp->timer);

  return PROFILE_SUCCESS;
}

int lj_sysprof_stop(lua_State *L)
{
  struct sysprof *sp = &sysprof;
  global_State *g = sp->g;
  struct lj_wbuf *out = &sp->out;

  if (SPS_IDLE == sp->state)
    return PROFILE_ERRRUN;
  else if (G(L) != g)
    return PROFILE_ERRUSE;

  lj_profile_timer_stop(&sp->timer);

  if (SPS_HALT == sp->state) {
    errno = sp->saved_errno;
    sp->state = SPS_IDLE;
    /* wbuf was terminated when error occurred. */
    return PROFILE_ERRIO;
  }

  sp->state = SPS_IDLE;

  if (stream_is_needed(sp)) {
    int cb_status = 0;

    stream_epilogue(sp);
    lj_wbuf_flush(out);

    cb_status = sp->on_stop(sp->opt.ctx, out->buf);
    if (LJ_UNLIKELY(lj_wbuf_test_flag(out, STREAM_ERRIO | STREAM_STOP)) ||
        cb_status != 0) {
      errno = lj_wbuf_errno(out);
      lj_wbuf_terminate(out);
      return PROFILE_ERRIO;
    }

    lj_wbuf_terminate(out);
  }

  return PROFILE_SUCCESS;
}

int lj_sysprof_report(struct luam_Sysprof_Counters *counters)
{
  const struct sysprof *sp = &sysprof;
  memcpy(counters, &sp->counters, sizeof(sp->counters));
  return PROFILE_SUCCESS;
}

void lj_sysprof_add_proto(const struct GCproto *pt)
{
  struct sysprof *sp = &sysprof;

  if (sp->state != SPS_PROFILE || sp->opt.mode == LUAM_SYSPROF_DEFAULT)
    return;

  /*
  ** XXX: Avoid sampling during the symtab extension. That shouldn't have any
  ** significant effect on profile precision, but if it does, it's better to
  ** implement an async-safe queue for the symtab events.
  */
  sp->state = SPS_IDLE;
  lj_wbuf_addbyte(&sp->out, LJP_SYMTAB_LFUNC_EVENT);
  lj_symtab_dump_proto(&sp->out, pt);
  sp->state = SPS_PROFILE;
}

#if LJ_HASJIT
void lj_sysprof_add_trace(const struct GCtrace *tr)
{
  struct sysprof *sp = &sysprof;

  if (sp->state != SPS_PROFILE || sp->opt.mode == LUAM_SYSPROF_DEFAULT)
    return;

  /* See the comment about the sysprof state above. */
  sp->state = SPS_IDLE;
  lj_wbuf_addbyte(&sp->out, LJP_SYMTAB_TRACE_EVENT);
  lj_symtab_dump_trace(&sp->out, tr);
  sp->state = SPS_PROFILE;
}
#endif /* LJ_HASJIT */

#else /* LJ_HASSYSPROF */

int lj_sysprof_set_writer(luam_Sysprof_writer writer) {
  UNUSED(writer);
  return PROFILE_ERRUSE;
}

int lj_sysprof_set_on_stop(luam_Sysprof_on_stop on_stop) {
  UNUSED(on_stop);
  return PROFILE_ERRUSE;
}

int lj_sysprof_set_backtracer(luam_Sysprof_backtracer backtracer) {
  UNUSED(backtracer);
  return PROFILE_ERRUSE;
}

int lj_sysprof_start(lua_State *L, const struct luam_Sysprof_Options *opt)
{
  UNUSED(L);
  return PROFILE_ERRUSE;
}

int lj_sysprof_stop(lua_State *L)
{
  UNUSED(L);
  return PROFILE_ERRUSE;
}

int lj_sysprof_report(struct luam_Sysprof_Counters *counters)
{
  UNUSED(counters);
  return PROFILE_ERRUSE;
}

void lj_sysprof_add_proto(const struct GCproto *pt)
{
  UNUSED(pt);
}

#if LJ_HASJIT
void lj_sysprof_add_trace(const struct GCtrace *tr)
{
  UNUSED(tr);
}
#endif /* LJ_HASJIT */

#endif /* LJ_HASSYSPROF */
