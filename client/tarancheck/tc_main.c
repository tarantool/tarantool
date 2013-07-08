
/*
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_snapshot.h>
#include <connector/c/include/tarantool/tnt_dir.h>

#include <third_party/gopt/gopt.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include "tc_key.h"
#define MH_SOURCE 1
#include "tc_hash.h"
#include "tc_options.h"
#include "tc_config.h"
#include "tc_space.h"
#include "tc_generate.h"
#include "tc_verify.h"

void out_warning(ConfettyError v, char *format, ...) {
	(void)v;
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	printf("\n");
	va_end(ap);
}

int main(int argc, char *argv[])
{
	struct tc_options opts;
	tc_options_init(&opts);

	int rc;
	enum tc_options_mode mode = tc_options_process(&opts, argc, argv);
	switch (mode) {
	case TC_MODE_USAGE:
		return tc_options_usage();
	case TC_MODE_VERSION:
		return 0;
	case TC_MODE_VERIFY:
		rc = tc_config_load(&opts);
		if (rc == -1)
			return 1;
		rc = tc_verify(&opts);
		if (rc == -1)
			break;
		break;
	case TC_MODE_GENERATE:
		rc = tc_config_load(&opts);
		if (rc == -1)
			return 1;
		rc = tc_generate(&opts);
		if (rc == -1)
			break;
		break;
	}

	tc_options_free(&opts);
	return 0;
}
