#ifndef TARANTOOL_WARNING_H
#define TARANTOOL_WARNING_H

#include "prscfg.h"

/** Begin logging - must be called before load_cfg() . */
extern FILE *cfg_out;
extern char *cfg_log;
extern size_t cfg_logsize;

void out_warning(ConfettyError r, const char *format, ...);

#endif
