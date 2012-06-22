#ifndef TC_OPT_H_INCLUDED
#define TC_OPT_H_INCLUDED

/*
 * Copyright (C) 2012 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#define TC_VERSION_MAJOR "0"
#define TC_VERSION_MINOR "1"

enum tc_opt_mode {
	TC_OPT_USAGE,
	TC_OPT_VERSION,
	TC_OPT_RPL,
	TC_OPT_WAL_CAT,
	TC_OPT_WAL_PLAY,
	TC_OPT_CMD,
	TC_OPT_INTERACTIVE
};

struct tc_opt {
	enum tc_opt_mode mode;
	const char *host;
	int port;
	int port_admin;
	uint64_t lsn;
	const char *xlog;
	char **cmdv;
	int cmdc;
};

void tc_opt_usage(void);
void tc_opt_version(void);

enum tc_opt_mode
tc_opt_init(struct tc_opt *opt, int argc, char **argv);

#endif /* TC_OPT_H_INCLUDED */
