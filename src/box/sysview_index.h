#ifndef TARANTOOL_BOX_SYSVIEW_INDEX_H_INCLUDED
#define TARANTOOL_BOX_SYSVIEW_INDEX_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
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

#include "index.h"

struct sysview_iterator;

typedef bool (*sysview_filter_f)(struct space *source, struct tuple *);

class SysviewIndex: public Index {
public:
	SysviewIndex(struct key_def *key_def, uint32_t source_space_id,
		     uint32_t source_index_id, sysview_filter_f filter);
	virtual ~SysviewIndex() override;
	virtual struct tuple *findByKey(const char *key,
					uint32_t part_count) const override;

	virtual struct iterator *allocIterator() const override;
	virtual void initIterator(struct iterator *iterator,
				  enum iterator_type type,
				  const char *key,
				  uint32_t part_count) const override;

	uint32_t source_space_id;
	uint32_t source_index_id;
	sysview_filter_f filter;
};

class SysviewVspaceIndex: public SysviewIndex {
public:
	SysviewVspaceIndex(struct key_def *key_def);
};

class SysviewVindexIndex: public SysviewIndex {
public:
	SysviewVindexIndex(struct key_def *key_def);
};

class SysviewVuserIndex: public SysviewIndex {
public:
	SysviewVuserIndex(struct key_def *key_def);
};

class SysviewVprivIndex: public SysviewIndex {
public:
	SysviewVprivIndex(struct key_def *key_def);
};

class SysviewVfuncIndex: public SysviewIndex {
public:
	SysviewVfuncIndex(struct key_def *key_def);
};

#endif /* TARANTOOL_BOX_SYSVIEW_INDEX_H_INCLUDED */
