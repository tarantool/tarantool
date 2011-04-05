/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met: 1. Redistributions of source code must
 * retain the above copyright notice, this list of conditions and
 * the following disclaimer.  2. Redistributions in binary form
 * must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "diagnostics.h"
#include "fiber.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static struct Error oom_error = { ENOMEM, "Out of memory" };

/**
 * Allocate error on heap. Errors are expected to be rare and
 * small, thus we don't care much about allocation speed, and
 * memory fragmentation should be negligible.
 */

static struct Error *error_create(int code, const char *msg_arg)
{
	/* Just something large enough. */
	const int MAX_MSGLEN = 200;

	if (msg_arg == NULL)
		msg_arg = "";

	size_t msglen = strlen(msg_arg);
	char *msg;

	if (msglen > MAX_MSGLEN)
		msglen = MAX_MSGLEN;

	struct Error *error = malloc(sizeof(struct Error) + msglen + 1);

	if (error == NULL)
		return &oom_error;

	msg = (char *)(error + 1);
	strncpy(msg, msg_arg, msglen);
	msg[msglen] = '\0';

	error->code = code;
	error->msg = msg;

	return error;
}


static void error_destroy(struct Error *error)
{
	if (error != &oom_error)
		free(error);
}


void diag_set_error(int code, const char *message)
{
	if (fiber->diagnostics)
		diag_clear();
	fiber->diagnostics = error_create(code, message);
}


struct Error *diag_get_last_error()
{
	return fiber->diagnostics;
}


void diag_clear()
{
	error_destroy(fiber->diagnostics);
	fiber->diagnostics = NULL;
}
