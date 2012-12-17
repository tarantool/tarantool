/*
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
#include "hash_index.h"
#include "tree_index.h"
#include "tuple.h"
#include "say.h"
#include "exception.h"
#include "space.h"

static struct index_traits index_traits = {
	.allows_partial_key = false,
};

const char *field_data_type_strs[] = {"NUM", "NUM64", "STR", "\0"};
STRS(index_type, INDEX_TYPE);
STRS(iterator_type, ITERATOR_TYPE);

/* {{{ Utilities. **********************************************/

void
check_key_parts(const struct key_def *key_def,
		int part_count, bool partial_key_allowed)
{
	if (part_count > key_def->part_count)
		tnt_raise(ClientError, :ER_KEY_PART_COUNT,
			  part_count, key_def->part_count);
	if (!partial_key_allowed && part_count < key_def->part_count)
		tnt_raise(ClientError, :ER_EXACT_MATCH,
			  part_count, key_def->part_count);
}

/**
 * Check if replacement of an old tuple with a new one is
 * allowed.
 */
uint32_t
replace_check_dup(struct tuple *old_tuple,
		  struct tuple *dup_tuple,
		  enum dup_replace_mode mode)
{
	if (dup_tuple == NULL) {
		if (mode == DUP_REPLACE) {
			/*
			 * dup_replace_mode is DUP_REPLACE, and
			 * a tuple with the same key is not found.
			 */
			return ER_TUPLE_NOT_FOUND;
		}
	} else { /* dup_tuple != NULL */
		if (dup_tuple != old_tuple &&
		    (old_tuple != NULL || mode == DUP_INSERT)) {
			/*
			 * There is a duplicate of new_tuple,
			 * and it's not old_tuple: we can't
			 * possibly delete more than one tuple
			 * at once.
			 */
			return ER_TUPLE_FOUND;
		}
	}
	return 0;
}

/* }}} */

/* {{{ Index -- base class for all indexes. ********************/

@implementation Index

+ (struct index_traits *) traits
{
	return &index_traits;
}

+ (Index *) alloc: (enum index_type) type
	 :(struct key_def *) key_def
	 :(struct space *) space
{
	switch (type) {
	case HASH:
		return [HashIndex alloc: key_def :space];
	case TREE:
		return [TreeIndex alloc: key_def :space];
	default:
		assert(false);
	}
}

- (id) init: (struct key_def *) key_def_arg :(struct space *) space_arg;
{
	self = [super init];
	if (self == NULL)
		return NULL;

	traits = [object_getClass(self) traits];
	key_def = key_def_arg;
	space = space_arg;
	position = [self allocIterator];

	return self;
}

- (void) free
{
	position->free(position);
	[super free];
}

- (void) beginBuild
{
	[self subclassResponsibility: _cmd];
}

- (void) buildNext: (struct tuple *)tuple
{
	(void) tuple;
	[self subclassResponsibility: _cmd];
}

- (void) endBuild
{
	[self subclassResponsibility: _cmd];
}

- (void) build: (Index *) pk
{
	(void) pk;
	[self subclassResponsibility: _cmd];
}


- (size_t) size
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (struct tuple *) min
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) max
{
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) findByKey: (void *) key :(int) part_count
{
	(void) key;
	(void) part_count;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct tuple *) replace: (struct tuple *) old_tuple
			  : (struct tuple *) new_tuple
			  : (enum dup_replace_mode) mode
{
	(void) old_tuple;
	(void) new_tuple;
	(void) mode;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (struct iterator *) allocIterator
{
	[self subclassResponsibility: _cmd];
	return NULL;
}


- (void) initIterator: (struct iterator *) iterator
	:(enum iterator_type) type
	:(void *) key :(int) part_count
{
	(void) iterator;
	(void) type;
	(void) key;
	(void) part_count;
	[self subclassResponsibility: _cmd];
}

@end

/* }}} */
