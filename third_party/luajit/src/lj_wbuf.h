/*
** Low-level event streaming for LuaJIT Profiler.
**
** XXX: Please note that all events may not be streamed inside a signal handler
** due to using default memcpy from glibc as not async-signal-safe function.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#ifndef _LJ_WBUF_H
#define _LJ_WBUF_H

#include "lj_def.h"

/*
** Data format for strings:
**
** string         := string-len string-payload
** string-len     := <ULEB128>
** string-payload := <BYTE> {string-len}
**
** Note.
** For strings shorter than 128 bytes (most likely scenario in our case)
** we write the same amount of data (1-byte ULEB128 + actual payload) as we
** would have written with straightforward serialization (actual payload + \0),
** but make parsing easier.
*/

/* Stream errors. */
#define STREAM_ERRIO 0x1
#define STREAM_STOP   0x2

/*
** Buffer writer which is called on the buffer flush.
** Should return amount of written bytes on success or zero in case of error.
** *data should contain a buffer of at least the initial size.
** If *data == NULL stream stops.
*/
typedef size_t (*lj_wbuf_writer)(const void **data, size_t len, void *opt);

/* Write buffer. */
struct lj_wbuf {
  lj_wbuf_writer writer;
  /* Context for writer function. */
  void *ctx;
  /* Buffer size. */
  size_t size;
  /* Start of buffer. */
  uint8_t *buf;
  /* Current position in buffer. */
  uint8_t *pos;
  /* Saved errno in case of error. */
  int saved_errno;
  /* Internal flags. */
  volatile uint8_t flags;
};

/* Initialize the buffer. */
void lj_wbuf_init(struct lj_wbuf *buf, lj_wbuf_writer writer, void *ctx,
		  uint8_t *mem, size_t size);

/* Set pointers to NULL and reset flags and errno. */
void LJ_FASTCALL lj_wbuf_terminate(struct lj_wbuf *buf);

/* Write single byte to the buffer. */
void LJ_FASTCALL lj_wbuf_addbyte(struct lj_wbuf *buf, uint8_t b);

/* Write uint64_t in uleb128 format to the buffer. */
void LJ_FASTCALL lj_wbuf_addu64(struct lj_wbuf *buf, uint64_t n);

/* Writes n bytes from an arbitrary buffer src to the buffer. */
void lj_wbuf_addn(struct lj_wbuf *buf, const void *src, size_t n);

/* Write string to the buffer. */
void LJ_FASTCALL lj_wbuf_addstring(struct lj_wbuf *buf, const char *s);

/* Immediately flush the buffer. */
void LJ_FASTCALL lj_wbuf_flush(struct lj_wbuf *buf);

/* Check flags. */
int LJ_FASTCALL lj_wbuf_test_flag(const struct lj_wbuf *buf, uint8_t flag);

/* Return saved errno. */
int LJ_FASTCALL lj_wbuf_errno(const struct lj_wbuf *buf);

#endif
