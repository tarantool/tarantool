/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "space_upgrade.h"

#include <assert.h>
#include <stddef.h>

#include "msgpuck.h"
#include "space.h"
#include "space_def.h"
#include "trivia/config.h"

#if defined(ENABLE_SPACE_UPGRADE)
# error unimplemented
#endif

static struct space_upgrade_def {} dummy_def;

struct space_upgrade_def *
space_upgrade_def_decode(const char **data, struct region *region)
{
	(void)region;
	/**
	 * Option decoder may only fail with IllegalParams error so we return
	 * a non-NULL ptr here and abort later, in space_prepare_upgrade.
	 */
	mp_next(data);
	return &dummy_def;
}

struct space_upgrade_def *
space_upgrade_def_dup(const struct space_upgrade_def *def)
{
	assert(def == NULL || def == &dummy_def);
	return def != NULL ? &dummy_def : NULL;
}

void
space_upgrade_def_delete(struct space_upgrade_def *def)
{
	assert(def == NULL || def == &dummy_def);
	(void)def;
}

void
space_upgrade_run(struct space *space)
{
	assert(space->def->opts.upgrade_def == NULL);
	(void)space;
}
