/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_ETCD_CLIENT)
#include "lua/etcd_client_impl.h"
#else /* !defined(ENABLE_ETCD_CLIENT) */

#define ETCD_CLIENT_LUA_MODULES

#endif /* !defined(ENABLE_ETCD_CLIENT) */
