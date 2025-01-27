#include "memtx_sort_data.h"
#include "memtx_index_read_view.h"

#include "core/assoc.h"
#include "trivia/util.h"

/**
 * Map: (space_id, index_id) => (sort data entry information).
 */
struct memtx_sort_data_key {
	/** The space ID. */
	uint32_t space_id;
	/** The index ID. */
	uint32_t index_id;
};

static inline uint64_t
memtx_sort_data_key_hash(struct memtx_sort_data_key key)
{
	return (key.space_id << 8) | key.index_id;
}

static inline int
memtx_sort_data_key_cmp(struct memtx_sort_data_key a,
			struct memtx_sort_data_key b)
{
	return a.space_id != b.space_id || a.index_id != b.index_id;
}

#define mh_name _memtx_sort_data_entries
#define mh_key_t struct memtx_sort_data_key

/** The sort data file header entry (the index sort data information). */
struct memtx_sort_data_entry {
	/** The entry identifier. */
	struct memtx_sort_data_key key;
	/** The offset of the sort data in the file. */
	size_t offset;
	/** The phisical size of the sort data. */
	size_t psize;
	/** The amount of tuples stored. */
	size_t len;
	/** Offset to the entry key's "offset" value in the file header. */
	size_t offset_offset;
	/** Offset to the entry key's "size" value in the file header. */
	size_t psize_offset;
	/** Offset to the entry key's "length" value in the file header. */
	size_t len_offset;
	/** The sort data fully written into the file already. */
	bool is_committed;
};

#define mh_node_t struct memtx_sort_data_entry
#define mh_arg_t void *
#define mh_hash(a, arg) (memtx_sort_data_key_hash((a)->key))
#define mh_hash_key(a, arg) (memtx_sort_data_key_hash(a))
#define mh_cmp(a, b, arg) (memtx_sort_data_key_cmp((a)->key, (b)->key))
#define mh_cmp_key(a, b, arg) (memtx_sort_data_key_cmp(a, (b)->key))
#define MH_SOURCE
#include "salad/mhash.h"

/** The sort data file reader and writer context. */
struct memtx_sort_data {
	/** The sort data file pointer. */
	FILE *fp;
	/** The sort data file name. */
	char *fname;
	/** The memtx snapshot dir name. */
	char *dirname;
	/** The vclock signature of the sort data file. */
	int64_t signature;
	/** The sort data file instance signature. */
	const struct tt_uuid *instance_uuid;
	/** The information about the sort data available. */
	struct mh_memtx_sort_data_entries_t *entries;
	/** The currently handled sort data entry. */
	struct memtx_sort_data_entry *curr_entry;
	/** The number of elements remained in the file. */
	size_t curr_entry_len_remained;
	/** The buffer to store pre-read PK data in. */
	void **buffer;
	/** The buffer to store pre-read PK data in (capacity). */
	size_t buffer_capacity;
	/** The buffer to store pre-read PK data in (readable size). */
	size_t buffer_size;
	/** The buffer to store pre-read PK data in (current pointer). */
	size_t buffer_i;
	/** The old to n32 tuple address map used on recovery. */
	struct mh_ptrptr_t *old2new;
};

struct memtx_sort_data *
memtx_sort_data_new(struct read_view *rv, const char *dirname,
		    struct tt_uuid *instance_uuid)
{
	struct memtx_sort_data *msd = xcalloc(1, sizeof(*msd));
	msd->dirname = xstrdup(dirname);
	msd->signature = vclock_sum(&rv->vclock);
	msd->instance_uuid = instance_uuid;
	msd->entries = mh_memtx_sort_data_entries_new();

	/* Find out which spaces and indexes must be saved. */
	struct space_read_view *space_rv;
	read_view_foreach_space(space_rv, rv) {
		/* Don't save the sort data of system spaces. */
		if (space_id_is_system(space_rv->id))
			continue;

		/* Only create sort data for spaces with SKs. */
		bool need_sort_data = false;
		for (uint32_t i = 1; i <= space_rv->index_id_max; i++) {
			/* No read view created - skip. */
			if (space_rv->index_map[i] == NULL)
				continue;

			/* Not a memtx index - skip the whole space. */
			if (strcmp(space_rv->index_map[i]->def->engine_name,
				   "memtx") != 0) {
				break;
			}

			/* Does not support the sort data - skip. */
			struct memtx_index_read_view *rv =
				(struct memtx_index_read_view *)
					space_rv->index_map[i];
			if (rv->dump_sort_data == NULL)
				continue;

			/* Ok, let's do this. */
			struct memtx_sort_data_entry entry = {};
			entry.key.space_id = space_rv->id;
			entry.key.index_id = i;
			mh_memtx_sort_data_entries_put(msd->entries,
						       &entry, NULL, NULL);
			need_sort_data = true;
		}

		/* Insert the PK entry if required. */
		if (need_sort_data) {
			struct memtx_sort_data_entry entry = {};
			entry.key.space_id = space_rv->id;
			entry.key.index_id = 0; /* Just for clarity. */
			mh_memtx_sort_data_entries_put(msd->entries,
						       &entry, NULL, NULL);
		}
	}

	return msd;
}

void
memtx_sort_data_delete(struct memtx_sort_data *msd)
{
	assert(msd->fp == NULL);
	assert(msd->fname == NULL);
	mh_memtx_sort_data_entries_delete(msd->entries);
	free(msd->dirname);
	free(msd);
}

int
memtx_sort_data_open(struct memtx_sort_data *msd)
{
	msd->fname = xstrdup(
		tt_snprintf(PATH_MAX, "%s/%020lld.sortdata",
			    msd->dirname, (long long)msd->signature));
	msd->fp = fopen(msd->fname, "wb");
	if (msd->fp == NULL) {
		free(msd->fname);
		msd->fname = NULL;
		return -1;
	}

	/* Write the generic information. */
	fprintf(msd->fp, "SORTDATA\n");
	fprintf(msd->fp, "1\n");
	fprintf(msd->fp, "Instance: %s\n", tt_uuid_str(msd->instance_uuid));
	fprintf(msd->fp, "Entries: %u\n", mh_size(msd->entries));

	/* Write all the sort data entries. */
	mh_int_t i;
	mh_foreach(msd->entries, i) {
		struct memtx_sort_data_entry *entry =
			mh_memtx_sort_data_entries_node(msd->entries, i);
		fprintf(msd->fp, "%u/%u: ", entry->key.space_id,
			entry->key.index_id);
		entry->offset_offset = ftell(msd->fp);
		fprintf(msd->fp, "%016llx, ", 0LLU);
		entry->psize_offset = ftell(msd->fp);
		fprintf(msd->fp, "%016llx, ", 0LLU);
		entry->len_offset = ftell(msd->fp);
		fprintf(msd->fp, "%020llu\n", 0LLU);
	}
	fprintf(msd->fp, "\n");
	return 0;
}

void
memtx_sort_data_close(struct memtx_sort_data *msd)
{
	fclose(msd->fp);
	free(msd->fname);
	msd->fp = NULL;
	msd->fname = NULL;
}

void
memtx_sort_data_discard(struct memtx_sort_data *msd)
{
	unlink(msd->fname);
	memtx_sort_data_close(msd);
}

bool
memtx_sort_data_begin(struct memtx_sort_data *msd,
		      uint32_t space_id, uint32_t index_id)
{
	assert(msd->curr_entry == NULL);
	struct memtx_sort_data_key key = {space_id, index_id};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i == mh_end(msd->entries))
		return false; /* Not included index. */

	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(msd->entries, i);
	assert(!entry->is_committed);
	fseek(msd->fp, 0, SEEK_END);
	entry->offset = ftell(msd->fp);
	entry->psize = 0; /* Just for clarity. */
	entry->len = 0; /* Ditto. */
	msd->curr_entry = entry;
	return true;
}

int
memtx_sort_data_write(struct memtx_sort_data *msd,
		      void *data, size_t size, size_t count)
{
	if (msd->curr_entry == NULL)
		return 0; /* Not included index. */

	if (fwrite(data, size, count, msd->fp) != count)
		return -1;
	msd->curr_entry->psize += size * count;
	msd->curr_entry->len += count;
	return 0;
}

void
memtx_sort_data_commit(struct memtx_sort_data *msd)
{
	if (msd->curr_entry == NULL)
		return; /* Not included index. */

	fseek(msd->fp, msd->curr_entry->offset_offset, SEEK_SET);
	fprintf(msd->fp, "%016llx, ", (long long)msd->curr_entry->offset);
	fseek(msd->fp, msd->curr_entry->psize_offset, SEEK_SET);
	fprintf(msd->fp, "%016llx, ", (long long)msd->curr_entry->psize);
	fseek(msd->fp, msd->curr_entry->len_offset, SEEK_SET);
	fprintf(msd->fp, "%020llu\n", (long long)msd->curr_entry->len);
	msd->curr_entry->is_committed = true;
	msd->curr_entry = NULL;
}

static bool
memtx_sort_data_parse_entry(const char *fname, const char *line,
			    const char *entry_name, int base,
			    const char *entry_ptr, char **after_ptr,
			    const char *expect_after, long *result)
{
	*result = strtol(entry_ptr, after_ptr, base);
	if (memcmp(*after_ptr, expect_after, strlen(expect_after))) {
		say_error("%s: expected '%s' after %s:\n\t%s",
			  fname, expect_after, entry_name, line);
		return false;
	}
	*after_ptr += strlen(expect_after);
	return true;
}

struct memtx_sort_data *
memtx_sort_data_start_read(const char *dirname, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid)
{
	struct memtx_sort_data *msd = xcalloc(1, sizeof(*msd));
	msd->dirname = xstrdup(dirname);
	msd->signature = vclock_sum(vclock);
	msd->instance_uuid = instance_uuid;
	msd->entries = mh_memtx_sort_data_entries_new();
	msd->curr_entry_len_remained = 0;;
	msd->buffer_capacity = 1024 * 1024; /* In elements. */
	msd->buffer = xcalloc(msd->buffer_capacity, sizeof(*msd->buffer));
	msd->buffer_size = 0;
	msd->buffer_i = 0;
	msd->old2new = mh_ptrptr_new();

	/* Open the sort data file for read. */
	msd->fname = xstrdup(
		tt_snprintf(PATH_MAX, "%s/%020lld.sortdata",
			    msd->dirname, (long long)msd->signature));
	msd->fp = fopen(msd->fname, "rb");
	if (msd->fp == NULL)
		goto fail_free; /* No sort data file found. */

	/* Read the file header and fill the entry info. */
	char tmp[256] = {};
	const char *hdr_magic = "SORTDATA\n";
	const char *hdr_version = "1\n";

	/* Check the file magic. */
	if (fgets(tmp, sizeof(tmp), msd->fp) == NULL) {
		say_error("%s: failed to read file magic", msd->fname);
		goto fail_close;
	}
	if (strcmp(tmp, hdr_magic) != 0) {
		say_error("%s: file magic is invalid", msd->fname);
		goto fail_close;
	}

	/* Check the file version. */
	if (fgets(tmp, sizeof(tmp), msd->fp) == NULL) {
		say_error("%s: failed to read file version", msd->fname);
		goto fail_close;
	}
	if (strcmp(tmp, hdr_version) != 0) {
		say_error("%s: file version is unsupported", msd->fname);
		goto fail_close;
	}

	/* Get the header keys. */
	const char *key_instance = "Instance: ";
	const char *key_entries = "Entries: ";
	uint32_t read_entries = 0;
	while (fgets(tmp, sizeof(tmp), msd->fp)) {
		if (memcmp(tmp, key_instance, strlen(key_instance)) == 0) {
			char *uuid_str = tmp + strlen(key_instance);
			const size_t uuid_len = strlen(uuid_str) - 1; /* \n */
			if (uuid_len != UUID_STR_LEN) {
				say_error("%s: invalid UUID size", msd->fname);
				goto fail_close;
			}
			uuid_str[uuid_len] = '\0';
			struct tt_uuid uuid;
			if (tt_uuid_from_string(uuid_str, &uuid) != 0) {
				say_error("%s: invalid UUID", msd->fname);
				goto fail_close;
			}
			/* TODO: do we need the nil check? */
			if (!tt_uuid_is_nil(msd->instance_uuid) &&
			    !tt_uuid_is_equal(&uuid, msd->instance_uuid)) {
				say_error("%s: not matched UUID", msd->fname);
				goto fail_close;
			}
		} else if (memcmp(tmp, key_entries, strlen(key_entries)) == 0) {
			const char *entry_count_str = tmp + strlen(key_entries);
			read_entries = atoi(entry_count_str);
		} else if (read_entries) {
			char *space_id_str = tmp;
			char *index_id_str;
			long space_id;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "space ID", 10,
							 space_id_str,
							 &index_id_str,
							 "/", &space_id)) {
				goto fail_close;
			}

			char *offset_str;
			long index_id;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "index ID", 10,
							 index_id_str,
							 &offset_str,
							 ": ", &index_id)) {
				goto fail_close;
			}

			char *psize_str;
			long offset;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "data offset", 16,
							 offset_str, &psize_str,
							 ", ", &offset)) {
				goto fail_close;
			}

			char *len_str;
			long psize;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "phisical size", 16,
							 psize_str, &len_str,
							 ", ", &psize)) {
				goto fail_close;
			}

			char *end;
			uint64_t len = strtol(len_str, &end, 10);
			if (*end != '\n') {
				say_warn("%s: unexpected contents in the "
					 "sort data key, index skipped: %s",
					 msd->fname, tmp);
				continue;
			}

			/* Sanity check. */
			if ((len == 0) != (psize == 0)) {
				say_error("%s: entry size verification"
					  " failed", msd->fname);
				goto fail_close;
			}

			struct memtx_sort_data_entry entry = {};
			entry.key.space_id = space_id;
			entry.key.index_id = index_id;
			entry.offset = offset;
			entry.psize = psize;
			entry.len = len;
			mh_memtx_sort_data_entries_put(msd->entries,
						       &entry, NULL, NULL);

			read_entries--;
		} else if (tmp[0] == '\n') {
			break;
		}
	}
	say_info("using the memtx sort data from `%s'", msd->fname);
	return msd;

fail_close:
	fclose(msd->fp);
fail_free:
	say_warn("%s: ignored", msd->fname);
	mh_memtx_sort_data_entries_delete(msd->entries);
	mh_ptrptr_delete(msd->old2new);
	free(msd->buffer);
	free(msd->dirname);
	free(msd->fname);
	free(msd);
	return NULL;
}

void
memtx_sort_data_init_pk(struct memtx_sort_data *msd, uint32_t space_id)
{
	struct memtx_sort_data_key key = {space_id, 0};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i == mh_end(msd->entries)) {
		msd->curr_entry = NULL; /* No sort data or it's corrupted. */
		return;
	}

	/* Clear the old2new map if filled previously. */
	mh_ptrptr_clear(msd->old2new);
	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(msd->entries, i);
	fseek(msd->fp, entry->offset, SEEK_SET);
	mh_ptrptr_reserve(msd->old2new, entry->len, NULL);
	msd->curr_entry = entry;
	if (msd->curr_entry_len_remained != 0)
		panic("Not all sort data for a PK had ben used.");
	msd->curr_entry_len_remained = entry->len;
}

void
memtx_sort_data_map_next_pk_tuple(struct memtx_sort_data *msd,
				  struct tuple *new_ptr)
{
	if (msd->curr_entry == NULL)
		return; /* No sort data or it's corrupted. */

	struct mh_ptrptr_node_t node;
	if (msd->buffer_i >= msd->buffer_size) {
		/* We must have PK data to read if we've requested one. */
		assert((size_t)ftell(msd->fp) <
		       msd->curr_entry->offset + msd->curr_entry->psize);
		msd->buffer_size = MIN(msd->buffer_capacity,
				       msd->curr_entry_len_remained);
		if (fread(msd->buffer, sizeof(*msd->buffer),
			  msd->buffer_size, msd->fp) != msd->buffer_size) {
			/* The sort data is corrupted, clear all the entries. */
			say_error("%s: space %u: PK read failed, file ignored",
				  msd->fname, msd->curr_entry->key.space_id);
			msd->curr_entry = NULL;
			mh_memtx_sort_data_entries_clear(msd->entries);
			return;
		}
		msd->curr_entry_len_remained -= msd->buffer_size;
		msd->buffer_i = 0;
	}
	node.key = msd->buffer[msd->buffer_i++];
	node.val = new_ptr;
	mh_ptrptr_put(msd->old2new, &node, NULL, NULL);
}

bool
memtx_sort_data_seek_index(struct memtx_sort_data *msd, uint32_t index_id)
{
	if (msd->curr_entry == NULL)
		return false; /* No sort data or it's corrupted. */

	assert(index_id != 0);
	uint32_t space_id = msd->curr_entry->key.space_id;
	struct memtx_sort_data_key key = {space_id, index_id};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i == mh_end(msd->entries))
		return false; /* No SK sort data. */

	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(msd->entries, i);
	fseek(msd->fp, entry->offset, SEEK_SET);
	msd->curr_entry = entry;
	return true;
}

size_t
memtx_sort_data_size(struct memtx_sort_data *msd)
{
	assert(msd->curr_entry != NULL); /* Only called if sort data exists. */
	return msd->curr_entry->psize;
}

int
memtx_sort_data_read(struct memtx_sort_data *msd, void *buffer)
{
	assert(msd->curr_entry != NULL); /* Only called if sort data exists. */
	return fread(buffer, msd->curr_entry->psize, 1, msd->fp) == 1 ? 0 : -1;
}

struct tuple *
memtx_sort_data_resolve_tuple(struct memtx_sort_data *msd,
			      struct tuple *old_ptr)
{
	assert(msd->curr_entry != NULL); /* Only called if sort data exists. */
	mh_int_t i = mh_ptrptr_find(msd->old2new, old_ptr, NULL);
	if (i == mh_end(msd->old2new)) {
		/* The sort data is corrupted, clear all the entries. */
		say_error("%s: space %u: tuple resolve failed, file ignored",
			  msd->fname, msd->curr_entry->key.space_id);
		msd->curr_entry = NULL;
		mh_memtx_sort_data_entries_clear(msd->entries);
		return NULL;
	}
	return mh_ptrptr_node(msd->old2new, i)->val;
}

void
memtx_sort_data_reader_delete(struct memtx_sort_data *msd)
{
	fclose(msd->fp);
	mh_memtx_sort_data_entries_delete(msd->entries);
	mh_ptrptr_delete(msd->old2new);
	free(msd->buffer);
	free(msd->dirname);
	free(msd->fname);
	free(msd);
}
