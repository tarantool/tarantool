#ifndef TARANTOOL_WARNING_H
#define TARANTOOL_WARNING_H

#include "prscfg.h"

extern struct tbuf *cfg_out;

void out_warning(ConfettyError r, char *format, ...);

#endif
