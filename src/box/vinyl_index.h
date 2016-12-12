#ifndef TARANTOOL_BOX_VINYL_INDEX_H_INCLUDED
#define TARANTOOL_BOX_VINYL_INDEX_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

/**
 * A base class for primary and secondary Vinyl indexes.
 *
 * Vinyl primary and secondary indexes work differently:
 *
 * - the primary index is fully covering (also known as
 *   "clustered" in MS SQL circles).
 *   It stores all tuple fields of the tuple coming from
 *   INSERT/REPLACE/UPDATE operations. This index is
 *   the only place where the full tuple is stored.
 *
 * - a secondary index only stores parts participating in the
 *   secondary key, coalesced with parts of the primary key.
 *   Duplicate parts, i.e. identical parts of the primary and
 *   secondary key are only stored once. (@sa key_def_merge
 *   function). This reduces the disk and RAM space necessary to
 *   maintain a secondary index, but adds an extra look-up in the
 *   primary key for every fetched tuple.
 *
 * When a search in a secondary index is made, we first look up
 * the secondary index tuple, containing the primary key, and then
 * use this key to find the original tuple in the primary index.
 */
class VinylIndex: public Index
{
public:
	VinylIndex(struct vy_env *env, struct key_def *key_def);

	virtual void
	open() = 0;

	virtual struct tuple*
	replace(struct tuple*,
	        struct tuple*, enum dup_replace_mode) override;

	virtual struct tuple*
	findByKey(const char *key, uint32_t) const override;

	virtual struct iterator*
	allocIterator() const override;

	virtual void
	initIterator(struct iterator *iterator,
		     enum iterator_type type,
		     const char *key, uint32_t part_count) const override;

	virtual size_t
	bsize() const override;

	virtual struct tuple *
	min(const char *key, uint32_t part_count) const override;

	virtual struct tuple *
	max(const char *key, uint32_t part_count) const override;

	virtual size_t
	count(enum iterator_type type, const char *key, uint32_t part_count)
		const override;

	virtual struct tuple *
	iterator_next(struct vy_tx *tx, struct vinyl_iterator *it) const;

public:
	struct vy_env *env;
	struct vy_index *db;
};

class VinylPrimaryIndex: public VinylIndex
{
public:
	VinylPrimaryIndex(struct vy_env *env, struct key_def *key_def_arg);

	virtual void
	open() override;
};

/**
 * While the primary index has only one key_def that is
 * used for validating tuples, secondary index needs four:
 *
 * - the first one is defined by the user. It contains the key
 *   parts of the secondary key, as present in the original tuple.
 *   This is Index::key_def.
 *
 * - the second one is used to fetch key parts of the secondary
 *   key, *augmented* with the parts of the primary key from the
 *   original tuple. These parts concatenated together construe the
 *   tuple of the secondary key, i.e. the tuple stored. This is
 *   VinylSecondaryIndex::key_def_tuple_to_key.
 *
 * - the third one is used to compare tuples of the secondary key
 *   between each other. This is vy_index::key_def, it is also
 *   known as key_def_tuple_to_key in the code.
 *   @sa key_def_build_secondary()
 *
 * - the last one is used to build a key for lookup in the primary
 *   index, based on the tuple fetched from the secondary index.
 *   This is key_def_secondary_to_primary.
 *   @sa key_def_build_secondary_to_primary()
 */
class VinylSecondaryIndex: public VinylIndex
{
public:
	VinylSecondaryIndex(struct vy_env *env_arg, VinylPrimaryIndex *pk_arg,
			    struct key_def *key_def_arg);

	virtual struct tuple*
	findByKey(const char *key, uint32_t) const override;

	virtual void
	open() override;

	virtual struct tuple *
	iterator_next(struct vy_tx *tx, struct vinyl_iterator *it) const override;

	virtual ~VinylSecondaryIndex() override;

public:
	/** To fetch the secondary index tuple from original tuple */
	struct key_def *key_def_tuple_to_key;
	/** To fetch the primary key from the secondary index tuple. */
	struct key_def *key_def_secondary_to_primary;
	/**
	 * column_mask is the bitmask in that bit 'n' is set if
	 * key_def (@sa struct Index) parts contains a part with
	 * fieldno equal to 'n'. This mask is used for update
	 * optimization (@sa VinylSpace::executeUpdate).
	 */
	uint64_t column_mask;
	VinylPrimaryIndex *primary_index;
};

#endif /* TARANTOOL_BOX_VINYL_INDEX_H_INCLUDED */
