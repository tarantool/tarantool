/*
** Low-level writer for LuaJIT.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lj_wbuf_c
#define LUA_CORE

#include <errno.h>

#include "lj_obj.h"
#include "lj_wbuf.h"
#include "lj_utils.h"

static LJ_AINLINE void wbuf_set_flag(struct lj_wbuf *buf, uint8_t flag)
{
  buf->flags |= flag;
}

static LJ_AINLINE void wbuf_save_errno(struct lj_wbuf *buf)
{
  buf->saved_errno = errno;
}

static LJ_AINLINE size_t wbuf_len(const struct lj_wbuf *buf)
{
  return (size_t)(buf->pos - buf->buf);
}

static LJ_AINLINE size_t wbuf_left(const struct lj_wbuf *buf)
{
  return buf->size - wbuf_len(buf);
}

void lj_wbuf_init(struct lj_wbuf *buf, lj_wbuf_writer writer,
		  void *ctx, uint8_t *mem, size_t size)
{
  buf->ctx = ctx;
  buf->writer = writer;
  buf->buf = mem;
  buf->pos = mem;
  buf->size = size;
  buf->flags = 0;
  buf->saved_errno = 0;
}

void LJ_FASTCALL lj_wbuf_terminate(struct lj_wbuf *buf)
{
  lj_wbuf_init(buf, NULL, NULL, NULL, 0);
}

static LJ_AINLINE void wbuf_reserve(struct lj_wbuf *buf, size_t n)
{
  lj_assertX(n <= buf->size, "wbuf overflow");
  if (LJ_UNLIKELY(wbuf_left(buf) < n))
    lj_wbuf_flush(buf);
}

/* Writes a byte to the output buffer. */
void LJ_FASTCALL lj_wbuf_addbyte(struct lj_wbuf *buf, uint8_t b)
{
  if (LJ_UNLIKELY(lj_wbuf_test_flag(buf, STREAM_STOP)))
    return;
  wbuf_reserve(buf, sizeof(b));
  *buf->pos++ = b;
}

/* Writes an unsigned integer which is at most 64 bits long to the output. */
void LJ_FASTCALL lj_wbuf_addu64(struct lj_wbuf *buf, uint64_t n)
{
  if (LJ_UNLIKELY(lj_wbuf_test_flag(buf, STREAM_STOP)))
    return;
  wbuf_reserve(buf, LEB128_U64_MAXSIZE);
  buf->pos += (ptrdiff_t)lj_utils_write_uleb128(buf->pos, n);
}

/* Writes n bytes from an arbitrary buffer src to the buffer. */
void lj_wbuf_addn(struct lj_wbuf *buf, const void *src, size_t n)
{
  if (LJ_UNLIKELY(lj_wbuf_test_flag(buf, STREAM_STOP)))
    return;
  /*
  ** Very unlikely: We are told to write a large buffer at once.
  ** Buffer doesn't belong to us so we must to pump data
  ** through the buffer.
  */
  while (LJ_UNLIKELY(n > buf->size)) {
    const size_t left = wbuf_left(buf);
    memcpy(buf->pos, src, left);
    buf->pos += (ptrdiff_t)left;
    lj_wbuf_flush(buf);
    src = (uint8_t *)src + (ptrdiff_t)left;
    n -= left;
  }

  wbuf_reserve(buf, n);
  memcpy(buf->pos, src, n);
  buf->pos += (ptrdiff_t)n;
}

/* Writes a \0-terminated C string to the output buffer. */
void LJ_FASTCALL lj_wbuf_addstring(struct lj_wbuf *buf, const char *s)
{
  const size_t l = strlen(s);

  /* Check that profiling is still active is made in the callee's scope. */
  lj_wbuf_addu64(buf, (uint64_t)l);
  lj_wbuf_addn(buf, s, l);
}

void LJ_FASTCALL lj_wbuf_flush(struct lj_wbuf *buf)
{
  const size_t len = wbuf_len(buf);
  size_t written;

  if (LJ_UNLIKELY(lj_wbuf_test_flag(buf, STREAM_STOP)))
    return;

  written = buf->writer((const void **)&buf->buf, len, buf->ctx);

  if (LJ_UNLIKELY(written < len)) {
    wbuf_set_flag(buf, STREAM_ERRIO);
    wbuf_save_errno(buf);
  }
  if (LJ_UNLIKELY(buf->buf == NULL)) {
    wbuf_set_flag(buf, STREAM_STOP);
    wbuf_save_errno(buf);
  }
  buf->pos = buf->buf;
}

int LJ_FASTCALL lj_wbuf_test_flag(const struct lj_wbuf *buf, uint8_t flag)
{
  return buf->flags & flag;
}

int LJ_FASTCALL lj_wbuf_errno(const struct lj_wbuf *buf)
{
  return buf->saved_errno;
}
