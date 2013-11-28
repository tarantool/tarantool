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
#include "tarantool/config.h"
#include <stddef.h>

#include <third_party/gopt/gopt.h>

const void *opt_def =
	gopt_start(gopt_option('g', GOPT_ARG, gopt_shorts(0),
			       gopt_longs("cfg-get", "cfg_get"),
			       "=KEY", "return a value from configuration file described by KEY"),
		   gopt_option('k', 0, gopt_shorts(0),
			       gopt_longs("check-config"),
			       NULL, "Check configuration file for errors"),
		   gopt_option('c', GOPT_ARG, gopt_shorts('c'),
			       gopt_longs("config"),
			       "=FILE", "path to configuration file (default: " DEFAULT_CFG_FILENAME ")"),
		   gopt_option('I', 0, gopt_shorts(0),
			       gopt_longs("init-storage", "init_storage"),
			       NULL, "initialize storage (an empty snapshot file) and exit"),
		   gopt_option('v', 0, gopt_shorts('v'), gopt_longs("verbose"),
			       NULL, "increase verbosity level in log messages"),
		   gopt_option('B', 0, gopt_shorts('B'), gopt_longs("background"),
			       NULL, "redirect input/output streams to a log file and run as daemon"),
		   gopt_option('h', 0, gopt_shorts('h', '?'), gopt_longs("help"),
			       NULL, "display this help and exit"),
		   gopt_option('V', 0, gopt_shorts('V'), gopt_longs("version"),
			       NULL, "print program version and exit")
);
