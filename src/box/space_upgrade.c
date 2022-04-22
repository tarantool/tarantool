/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "space_upgrade.h"

#include <assert.h>
#include <stddef.h>

#include "diag.h"
#include "error.h"
#include "space.h"
#include "space_def.h"
#include "trivia/config.h"

#if defined(ENABLE_SPACE_UPGRADE)
# error unimplemented
#endif

struct space_upgrade_def *
space_upgrade_def_decode(const char **data, struct region *region)
{
	(void)data;
	(void)region;
	diag_set(ClientError, ER_UNSUPPORTED,
		 "Community edition", "space upgrade");
	return NULL;
}

int
space_upgrade_check_alter(struct space *space, struct space_def *new_def)
{
	assert(space->upgrade == NULL);
	assert(new_def->opts.upgrade_def == NULL);
	(void)space;
	(void)new_def;
	return 0;
}

void
space_upgrade_run(struct space *space)
{
	assert(space->def->opts.upgrade_def == NULL);
	(void)space;
}
