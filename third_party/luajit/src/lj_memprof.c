/*
** Implementation of memory profiler.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lj_memprof_c
#define LUA_CORE

#include <errno.h>

#include "lj_arch.h"
#include "lj_memprof.h"

#if LJ_HASMEMPROF

#include "lj_obj.h"
#include "lj_frame.h"
#include "lj_debug.h"
#include "lj_symtab.h"

/* ---------------------------- Memory profiler ----------------------------- */

enum memprof_state {
  /* Memory profiler is not running. */
  MPS_IDLE,
  /* Memory profiler is running. */
  MPS_PROFILE,
  /*
  ** Stopped in case of stopped stream.
  ** Saved errno is returned to user at lj_memprof_stop.
  */
  MPS_HALT
};

struct alloc {
  lua_Alloc allocf; /* Allocating function. */
  void *state; /* Opaque allocator's state. */
};

struct memprof {
  global_State *g; /* Profiled VM. */
  enum memprof_state state; /* Internal state. */
  struct lj_wbuf out; /* Output accumulator. */
  struct alloc orig_alloc; /* Original allocator. */
  struct lj_memprof_options opt; /* Profiling options. */
  int saved_errno; /* Saved errno when profiler deinstrumented. */
  uint32_t lib_adds; /* Number of libs loaded. Monotonic. */
};

static struct memprof memprof = {0};

const unsigned char ljm_header[] = {'l', 'j', 'm', LJM_CURRENT_FORMAT_VERSION,
				    0x0, 0x0, 0x0};

static void memprof_write_lfunc(struct lj_wbuf *out, uint8_t aevent,
				GCfunc *fn, struct lua_State *L,
				cTValue *nextframe)
{
  /*
  ** Line equals to zero when LuaJIT is built with the
  ** -DLUAJIT_DISABLE_DEBUGINFO flag.
  */
  const BCLine line = lj_debug_frameline(L, fn, nextframe);

  if (line < 0) {
    /*
    ** Line is >= 0 if we are inside a Lua function.
    ** There are cases when the memory profiler attempts
    ** to attribute allocations triggered by JIT engine recording
    ** phase with a Lua function to be recorded. It this case,
    ** lj_debug_frameline() may return BC_NOPOS (i.e. a negative value).
    ** We report such allocations as internal in order not to confuse users.
    */
    lj_wbuf_addbyte(out, aevent | ASOURCE_INT);
  } else {
    /*
    ** As a prototype is a source of an allocation, it has
    ** already been inserted into the symtab: on the start
    ** of the profiling or right after its creation.
    */
    lj_wbuf_addbyte(out, aevent | ASOURCE_LFUNC);
    lj_wbuf_addu64(out, (uintptr_t)funcproto(fn));
    lj_wbuf_addu64(out, (uint64_t)line);
  }
}

static void memprof_write_cfunc(struct lj_wbuf *out, uint8_t aevent,
				const GCfunc *fn, lua_State *L,
				uint32_t *lib_adds)
{
#if LJ_HASRESOLVER
  /* Check if there are any new libs. */
  /*
  ** XXX: Leaving the `vmstate` unchanged leads to an infinite
  ** recursion, because allocations inside ELF parser are treated
  ** as C-side allocations by memrpof. Setting the `vmstate` to
  ** LJ_VMST_INTERP solves the issue.
  */
  global_State *g = G(L);
  const uint32_t ostate = g->vmstate;
  g->vmstate = ~LJ_VMST_INTERP;
  lj_symtab_dump_newc(lib_adds, out, AEVENT_SYMTAB | ASOURCE_CFUNC, L);
  /* Restore vmstate. */
  g->vmstate = ostate;
#else
  UNUSED(lib_adds);
#endif

  lj_wbuf_addbyte(out, aevent | ASOURCE_CFUNC);
  lj_wbuf_addu64(out, (uintptr_t)fn->c.f);
}

static void memprof_write_ffunc(struct lj_wbuf *out, uint8_t aevent,
				GCfunc *fn, struct lua_State *L,
				cTValue *frame, uint32_t *lib_adds)
{
  cTValue *pframe = frame_prev(frame);
  GCfunc *pfn = frame_func(pframe);

  /*
  ** XXX: If a fast function is called by a Lua function, report the
  ** Lua function for more meaningful output. Otherwise report the fast
  ** function as a C function.
  */
  if (pfn != NULL && isluafunc(pfn))
    memprof_write_lfunc(out, aevent, pfn, L, frame);
  else
    memprof_write_cfunc(out, aevent, fn, L, lib_adds);
}

static void memprof_write_func(struct memprof *mp, uint8_t aevent)
{
  struct lj_wbuf *out = &mp->out;
  lua_State *L = gco2th(gcref(mp->g->mem_L));
  cTValue *frame = L->base - 1;
  GCfunc *fn = frame_func(frame);

  if (isluafunc(fn))
    memprof_write_lfunc(out, aevent, fn, L, NULL);
  else if (isffunc(fn))
    memprof_write_ffunc(out, aevent, fn, L, frame, &mp->lib_adds);
  else if (iscfunc(fn))
    memprof_write_cfunc(out, aevent, fn, L, &mp->lib_adds);
  else
    lj_assertL(0, "unknown function type to write by memprof");
}

#if LJ_HASJIT

static void memprof_write_trace(struct memprof *mp, uint8_t aevent)
{
  struct lj_wbuf *out = &mp->out;
  const global_State *g = mp->g;
  const TraceNo traceno = g->vmstate;
  lj_wbuf_addbyte(out, aevent | ASOURCE_TRACE);
  lj_wbuf_addu64(out, (uint64_t)traceno);
}

#else

static void memprof_write_trace(struct memprof *mp, uint8_t aevent)
{
  UNUSED(mp);
  UNUSED(aevent);
  lj_assertX(0, "write trace memprof event without JIT");
}

#endif

static void memprof_write_hvmstate(struct memprof *mp, uint8_t aevent)
{
  lj_wbuf_addbyte(&mp->out, aevent | ASOURCE_INT);
}

typedef void (*memprof_writer)(struct memprof *mp, uint8_t aevent);

static const memprof_writer memprof_writers[] = {
  memprof_write_hvmstate, /* LJ_VMST_INTERP */
  memprof_write_func, /* LJ_VMST_LFUNC */
  memprof_write_func, /* LJ_VMST_FFUNC */
  memprof_write_func, /* LJ_VMST_CFUNC */
  memprof_write_hvmstate, /* LJ_VMST_GC */
  memprof_write_hvmstate, /* LJ_VMST_EXIT */
  memprof_write_hvmstate, /* LJ_VMST_RECORD */
  memprof_write_hvmstate, /* LJ_VMST_OPT */
  memprof_write_hvmstate, /* LJ_VMST_ASM */
  /*
  ** XXX: In ideal world, we should report allocations from traces as well.
  ** But since traces must follow the semantics of the original code,
  ** behaviour of Lua and JITted code must match 1:1 in terms of allocations,
  ** which makes using memprof with enabled JIT virtually redundant.
  ** But if one wants to investigate allocations with JIT enabled,
  ** memprof_write_trace() dumps trace number and mcode starting address
  ** to the binary output. It can be useful to compare with with jit.v or
  ** jit.dump outputs.
  */
  memprof_write_trace /* LJ_VMST_TRACE */
};

static void memprof_write_caller(struct memprof *mp, uint8_t aevent)
{
  const global_State *g = mp->g;
  const uint32_t _vmstate = (uint32_t)~g->vmstate;
  const uint32_t vmstate = _vmstate < LJ_VMST_TRACE ? _vmstate : LJ_VMST_TRACE;

  memprof_writers[vmstate](mp, aevent);
}

static void *memprof_allocf(void *ud, void *ptr, size_t osize, size_t nsize)
{
  struct memprof *mp = &memprof;
  const struct alloc *oalloc = &mp->orig_alloc;
  struct lj_wbuf *out = &mp->out;
  void *nptr;

  lj_assertX(MPS_PROFILE == mp->state, "bad memprof profile state");
  lj_assertX(oalloc->allocf != memprof_allocf,
	     "unexpected memprof old alloc function");
  lj_assertX(oalloc->allocf != NULL,
	     "uninitialized memprof old alloc function");
  lj_assertX(ud == oalloc->state, "bad old memprof profile state");

  nptr = oalloc->allocf(ud, ptr, osize, nsize);

  if (nsize == 0) {
    memprof_write_caller(mp, AEVENT_FREE);
    lj_wbuf_addu64(out, (uintptr_t)ptr);
    lj_wbuf_addu64(out, (uint64_t)osize);
  } else if (ptr == NULL) {
    memprof_write_caller(mp, AEVENT_ALLOC);
    lj_wbuf_addu64(out, (uintptr_t)nptr);
    lj_wbuf_addu64(out, (uint64_t)nsize);
  } else {
    memprof_write_caller(mp, AEVENT_REALLOC);
    lj_wbuf_addu64(out, (uintptr_t)ptr);
    lj_wbuf_addu64(out, (uint64_t)osize);
    lj_wbuf_addu64(out, (uintptr_t)nptr);
    lj_wbuf_addu64(out, (uint64_t)nsize);
  }

  /* Deinstrument memprof if required. */
  if (LJ_UNLIKELY(lj_wbuf_test_flag(out, STREAM_STOP)))
    lj_memprof_stop(mainthread(mp->g));

  return nptr;
}

int lj_memprof_start(struct lua_State *L, const struct lj_memprof_options *opt)
{
  struct memprof *mp = &memprof;
  struct lj_memprof_options *mp_opt = &mp->opt;
  struct alloc *oalloc = &mp->orig_alloc;
  const size_t ljm_header_len = sizeof(ljm_header) / sizeof(ljm_header[0]);

  lj_assertL(opt->writer != NULL, "uninitialized memprof writer");
  lj_assertL(opt->on_stop != NULL, "uninitialized on stop memprof callback");
  lj_assertL(opt->buf != NULL, "uninitialized memprof writer buffer");
  lj_assertL(opt->len != 0, "bad memprof writer buffer length");

  if (mp->state != MPS_IDLE) {
    /* Clean up resources. Ignore possible errors. */
    opt->on_stop(opt->ctx, opt->buf);
    return PROFILE_ERRRUN;
  }

  /* Discard possible old errno. */
  mp->saved_errno = 0;

  /* Init options. */
  memcpy(mp_opt, opt, sizeof(*opt));

  /* Init general fields. */
  mp->g = G(L);
  mp->state = MPS_PROFILE;

  /* Init output. */
  lj_wbuf_init(&mp->out, mp_opt->writer, mp_opt->ctx, mp_opt->buf, mp_opt->len);
  lj_symtab_dump(&mp->out, mp->g, &mp->lib_adds);

  /* Write prologue. */
  lj_wbuf_addn(&mp->out, ljm_header, ljm_header_len);

  if (LJ_UNLIKELY(lj_wbuf_test_flag(&mp->out, STREAM_ERRIO|STREAM_STOP))) {
    /* on_stop call may change errno value. */
    int saved_errno = lj_wbuf_errno(&mp->out);
    /* Ignore possible errors. mp->out.buf may be NULL here. */
    mp_opt->on_stop(mp_opt->ctx, mp->out.buf);
    lj_wbuf_terminate(&mp->out);
    mp->state = MPS_IDLE;
    errno = saved_errno;
    return PROFILE_ERRIO;
  }

  /* Override allocating function. */
  oalloc->allocf = lua_getallocf(L, &oalloc->state);
  lj_assertL(oalloc->allocf != NULL, "uninitialized memprof old alloc function");
  lj_assertL(oalloc->allocf != memprof_allocf,
	     "unexpected memprof old alloc function");
  lua_setallocf(L, memprof_allocf, oalloc->state);

  return PROFILE_SUCCESS;
}

int lj_memprof_stop(struct lua_State *L)
{
  struct memprof *mp = &memprof;
  struct lj_memprof_options *mp_opt = &mp->opt;
  struct alloc *oalloc = &mp->orig_alloc;
  struct lj_wbuf *out = &mp->out;
  int cb_status;

  if (mp->state == MPS_HALT) {
    errno = mp->saved_errno;
    mp->state = MPS_IDLE;
    /* wbuf was terminated before. */
    return PROFILE_ERRIO;
  }

  if (mp->state != MPS_PROFILE)
    return PROFILE_ERRRUN;

  if (mp->g != G(L))
    return PROFILE_ERRUSE;

  mp->state = MPS_IDLE;

  lj_assertL(mp->g != NULL, "uninitialized global state in memprof state");

  lj_assertL(memprof_allocf == lua_getallocf(L, NULL),
	     "bad current allocator function on memprof stop");
  lj_assertL(oalloc->allocf != NULL,
	     "uninitialized old alloc function on memprof stop");
  lua_setallocf(L, oalloc->allocf, oalloc->state);

  if (LJ_UNLIKELY(lj_wbuf_test_flag(out, STREAM_STOP))) {
    /* on_stop call may change errno value. */
    int saved_errno = lj_wbuf_errno(out);
    /* Ignore possible errors. out->buf may be NULL here. */
    mp_opt->on_stop(mp_opt->ctx, out->buf);
    errno = saved_errno;
    goto errio;
  }

  lj_wbuf_addbyte(out, LJM_EPILOGUE_HEADER);

  lj_wbuf_flush(out);

  cb_status = mp_opt->on_stop(mp_opt->ctx, out->buf);
  if (LJ_UNLIKELY(lj_wbuf_test_flag(out, STREAM_ERRIO|STREAM_STOP) ||
		  cb_status != 0)) {
    errno = lj_wbuf_errno(out);
    goto errio;
  }

  lj_wbuf_terminate(out);
  return PROFILE_SUCCESS;
errio:
  lj_wbuf_terminate(out);
  return PROFILE_ERRIO;
}

void lj_memprof_add_proto(const struct GCproto *pt)
{
  struct memprof *mp = &memprof;

  if (mp->state != MPS_PROFILE)
    return;

  lj_wbuf_addbyte(&mp->out, AEVENT_SYMTAB | ASOURCE_LFUNC);
  lj_symtab_dump_proto(&mp->out, pt);
}

#if LJ_HASJIT

void lj_memprof_add_trace(const struct GCtrace *tr)
{
  struct memprof *mp = &memprof;

  if (mp->state != MPS_PROFILE)
    return;

  lj_wbuf_addbyte(&mp->out, AEVENT_SYMTAB | ASOURCE_TRACE);
  lj_symtab_dump_trace(&mp->out, tr);
}

#endif /* LJ_HASJIT */

#else /* LJ_HASMEMPROF */

int lj_memprof_start(struct lua_State *L, const struct lj_memprof_options *opt)
{
  UNUSED(L);
  /* Clean up resources. Ignore possible errors. */
  opt->on_stop(opt->ctx, opt->buf);
  return PROFILE_ERRUSE;
}

int lj_memprof_stop(struct lua_State *L)
{
  UNUSED(L);
  return PROFILE_ERRUSE;
}

void lj_memprof_add_proto(const struct GCproto *pt)
{
  UNUSED(pt);
}

#endif /* LJ_HASMEMPROF */
