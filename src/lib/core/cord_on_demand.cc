/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "cord_on_demand.h"

#include <stdlib.h>

#include "fiber.h"
#include "trivia/util.h"

/**
 * RAII wrapper around a cord object.
 *
 * A thread-local cord object is created on the first call to the get() method
 * and destroyed when the thread exits.
 */
class CordOnDemand final {
public:
	static cord *get() noexcept
	{
		thread_local CordOnDemand singleton;
		return singleton.cord_ptr;
	}

private:
	struct cord *cord_ptr;

	CordOnDemand() noexcept
	{
		cord_ptr = static_cast<struct cord *>(
				xcalloc(1, sizeof(*cord_ptr)));
		cord_create(cord_ptr, NULL);
	}

	~CordOnDemand()
	{
		cord_exit(cord_ptr);
		cord_destroy(cord_ptr);
		free(cord_ptr);
	}

	CordOnDemand(CordOnDemand &other) = delete;
	CordOnDemand &operator=(CordOnDemand &other) = delete;
};

struct cord *
cord_on_demand(void)
{
	return CordOnDemand::get();
}
