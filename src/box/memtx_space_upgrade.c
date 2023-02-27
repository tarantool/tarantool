/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "memtx_space_upgrade.h"

#include <stddef.h>

#include "diag.h"
#include "errcode.h"
#include "space.h"
#include "space_def.h"

#if defined(ENABLE_SPACE_UPGRADE)
# error unimplemented
#endif

int
memtx_space_prepare_upgrade(struct space *old_space, struct space *new_space)
{
	(void)old_space;
	if (new_space->def->opts.upgrade_def != NULL) {
		diag_set(ClientError, ER_UNSUPPORTED, "Community edition",
			 "space upgrade");
		return -1;
	}
	return 0;
}
