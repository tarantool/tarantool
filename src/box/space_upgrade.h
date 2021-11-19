#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tt_uuid.h"

#include "trivia/util.h"

struct func;
struct space;
struct tuple;

/** Status of upgrade operation. */
enum space_upgrade_status {
	/**
	 * Upgrade has been launched: upgrade options are verified,
	 * insertion to _space_upgrade has been processed, space's
	 * format is updated to new (if any).
	 */
	SPACE_UPGRADE_INPROGRESS = 0,
	/**
	 * Set in case in-progress upgrade fails for whatever reason.
	 * User is supposed to update upgrade function and/or set the new
	 * format and re-run upgrade.
	 */
	SPACE_UPGRADE_ERROR = 1,
	/**
	 * Set if space to be upgraded is tested with given upgrade function
	 * and/or new format. No real-visible data changes occur.
	 */
	SPACE_UPGRADE_TEST = 2,
};

/** Structure incorporating all vital details concerning upgrade operation. */
struct space_upgrade {
	/**
	 * Id of the space being upgraded. Used to identify space
	 * in on_commit/on_rollback triggers which are set in
	 * on_replace_dd_space_upgrade.
	 */
	uint32_t space_id;
	/** Status of current upgrade. */
	enum space_upgrade_status status;
	/** Pointer to the upgrade function. */
	struct func *func;
	/**
	 * New format of the space. It is used only in TEST mode; during
	 * real upgrade space already features updated format
	 * (space->tuple_format).
	 */
	struct tuple_format *format;

	/**
	 * uuid of the host i.e. instance which launched upgrade process.
	 * All other instances are switched to read-only mode and apply
	 * only rows received from master.
	 */
	struct tt_uuid host_uuid;
};

extern const char *upgrade_status_strs[];

static inline enum space_upgrade_status
upgrade_status_by_name(const char *name, uint32_t name_len)
{
	if (strlcmp(name, name_len, "inprogress", strlen("inprogress")) == 0)
		return SPACE_UPGRADE_INPROGRESS;
	if (strlcmp(name, name_len, "test", strlen("test")) == 0)
		return SPACE_UPGRADE_TEST;
	if (strlcmp(name, name_len, "error", strlen("error")) == 0)
		return SPACE_UPGRADE_ERROR;
	unreachable();
	return SPACE_UPGRADE_ERROR;
}

/** Functions below are used in alter.cc so surround them with C++ guards. */
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Set read-only mode for replication instances. Save previous mode to
 * @a was_ro static variable. After the last upgrade is finished,
 * space_upgrade_reset_ro() should be called - it restores original read-only
 * mode.
 */
void
space_upgrade_set_ro(struct space_upgrade *upgrade);

void
space_upgrade_reset_ro(struct space_upgrade *upgrade);

/** Release resources related to space_upgrade and free structure itself. */
void
space_upgrade_delete(struct space_upgrade *upgrade);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

/**
 * Launch test run of upgrade: it does not modify data; only verifies that
 * tuples after upgrade met all required conditions.
 */
int
space_upgrade_test(uint32_t space_id);

/** Launch upgrade operation. */
int
space_upgrade(uint32_t space_id);
