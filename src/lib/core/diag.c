/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "diag.h"
#include "fiber.h"

/** True if the error message was dynamically allocated. */
static bool
error_msg_is_malloced(struct error *e)
{
	return e->errmsg != e->errmsg_buf;
}

void
error_ref(struct error *e)
{
	assert(e->refs >= 0);
	if (e->refs >= INT64_MAX)
		panic("too many references to error object");
	e->refs++;
}

void
error_unref(struct error *e)
{
	assert(e->refs > 0);
	struct error *to_delete = e;
	while (--to_delete->refs == 0) {
		/* Unlink error from lists completely.*/
		struct error *cause = to_delete->cause;
		assert(to_delete->effect == NULL);
		if (to_delete->cause != NULL) {
			to_delete->cause->effect = NULL;
			to_delete->cause = NULL;
		}
		if (error_msg_is_malloced(to_delete))
			free(to_delete->errmsg);
		error_payload_destroy(&to_delete->payload);
		to_delete->destroy(to_delete);
		if (cause == NULL)
			return;
		to_delete = cause;
	}
}

const struct error_field *
error_find_field(const struct error *e, const char *name)
{
	return error_payload_find(&e->payload, name);
}

int
error_set_prev(struct error *e, struct error *prev)
{
	/*
	 * Make sure that adding error won't result in cycles.
	 * Don't bother with sophisticated cycle-detection
	 * algorithms, simple iteration is OK since as a rule
	 * list contains a dozen errors at maximum.
	 */
	if (prev != NULL) {
		if (e == prev)
			return -1;
		if (prev->effect != NULL || e->effect != NULL) {
			/*
			 * e and prev are already compared, so start
			 * from prev->cause.
			 */
			struct error *tmp = prev->cause;
			while (tmp != NULL) {
				 if (tmp == e)
					return -1;
				tmp = tmp->cause;
			}
			/*
			 * Unlink new 'effect' node from its old
			 * list of 'cause' errors.
			 */
			error_unlink_effect(prev);
		}
		error_ref(prev);
		prev->effect = e;
	}
	/*
	 * At once error can feature only one reason.
	 * So unlink previous 'cause' node.
	 */
	if (e->cause != NULL) {
		e->cause->effect = NULL;
		error_unref(e->cause);
	}
	/* Set new 'prev' node. */
	e->cause = prev;
	return 0;
}

void
error_create(struct error *e,
	     error_f destroy, error_f raise, error_f log,
	     const struct type_info *type, const char *file, unsigned line)
{
	e->destroy = destroy;
	e->raise = raise;
	e->log = log;
	e->type = type;
	e->refs = 0;
	e->saved_errno = 0;
	e->code = 0;
	error_payload_create(&e->payload);
	if (file == NULL)
		file = "";
	error_set_location(e, file, line);
	e->errmsg = e->errmsg_buf;
	e->errmsg[0] = '\0';
	e->cause = NULL;
	e->effect = NULL;
}

void
error_set_location(struct error *e, const char *file, int line)
{
	snprintf(e->file, sizeof(e->file), "%s", file);
	e->line = line;
}

struct diag *
diag_get(void)
{
	return &fiber()->diag;
}

void
error_format_msg(struct error *e, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(e, format, ap);
	va_end(ap);
}

void
error_append_msg(struct error *e, const char *format, ...)
{
	va_list ap1, ap2;
	va_start(ap1, format);
	va_copy(ap2, ap1);
	int static_buf_size = sizeof(e->errmsg_buf);
	int curr_len = strlen(e->errmsg);
	int len = vsnprintf(NULL, 0, format, ap1);
	int new_size = curr_len + len + 1;
	va_end(ap1);

	if (new_size > static_buf_size) {
		char *new_buf = xmalloc(new_size);
		memcpy(new_buf, e->errmsg, curr_len + 1);
		if (error_msg_is_malloced(e))
			free(e->errmsg);
		e->errmsg = new_buf;
	}
	char *msg = e->errmsg + curr_len;
	int new_len = vsnprintf(msg, len + 1, format, ap2);
	VERIFY(new_len == len);
	va_end(ap2);
}

void
error_vformat_msg(struct error *e, const char *format, va_list ap)
{
	/* Copy the `ap' to call `vsnprintf' twice. */
	va_list ap_copy;
	va_copy(ap_copy, ap);
	if (error_msg_is_malloced(e)) {
		free(e->errmsg);
		e->errmsg = e->errmsg_buf;
	}
	int static_buf_size = sizeof(e->errmsg_buf);
	int len = vsnprintf(e->errmsg, static_buf_size, format, ap);
	bool is_truncated = len >= static_buf_size;
	if (is_truncated) {
		e->errmsg = xmalloc(len + 1);
		int new_len = vsnprintf(e->errmsg, len + 1, format, ap_copy);
		VERIFY(new_len == len);
	}
	va_end(ap_copy);
}
