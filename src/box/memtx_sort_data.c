#include "xlog.h"
#include "memtx_sort_data.h"

#include "core/assoc.h"
#include "trivia/util.h"

#define DIAG_SET(msd, ...) do { \
	diag_set(ClientError, ER_INVALID_SORTDATA_FILE, \
		 (msd)->fname, tt_sprintf(__VA_ARGS__)); \
} while (false)

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
	long offset;
	/** The physical size of the sort data. */
	long psize;
	/** The amount of tuples stored. Can be updated by reader in process. */
	long len;
	/** Writer-only: offset of offset field in the file header. */
	long offset_offset;
	/** Writer-only: offset of psize field in the file header. */
	long psize_offset;
	/** Writer-only: offset of len field in the file header. */
	long len_offset;
	/** Writer-only: the data is fully written into the file already. */
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
	/** The XLOG file directory. */
	struct xdir dir;
	/** The sort data file name. */
	char fname[PATH_MAX];
	/** The sort data file instance signature. */
	const struct tt_uuid *instance_uuid;
	/** The information about the sort data entries. */
	struct mh_memtx_sort_data_entries_t *entries;
	/** The currently handled sort data entry. */
	struct memtx_sort_data_entry *curr_entry;
	/** The total amount of tuples in saved spaces. */
	size_t cardinality;
	/** The offset to the "Cardinality" file header key. */
	long cardinality_offset;
};

/** The sort data reader context. */
struct memtx_sort_data_reader {
	/** The sort data information. */
	struct memtx_sort_data *msd;
	/** The buffer to pre-read PK data into. */
	void **buffer;
	/** The buffer to pre-read PK data into (capacity). */
	size_t buffer_capacity;
	/** The buffer to pre-read PK data into (readable size). */
	size_t buffer_size;
	/** The buffer to pre-read PK data into (current pointer). */
	size_t buffer_i;
	/** The old to new tuple address map used on recovery. */
	struct mh_ptrptr_t *old2new;
};

struct memtx_sort_data *
memtx_sort_data_writer_new_empty(const char *dirname,
				 const struct tt_uuid *instance_uuid)
{
	struct memtx_sort_data *msd = xmalloc(sizeof(*msd));

	msd->fp = NULL;
	xdir_create(&msd->dir, dirname, SORTDATA,
		    instance_uuid, &xlog_opts_default);
	memset(msd->fname, 0, sizeof(msd->fname));
	msd->instance_uuid = instance_uuid;
	msd->entries = mh_memtx_sort_data_entries_new();
	msd->curr_entry = NULL;
	msd->cardinality = 0;
	msd->cardinality_offset = 0;
	return msd;
}

/* {{{ Writer *****************************************************************/

struct memtx_sort_data *
memtx_sort_data_writer_new(struct read_view *rv, const char *dirname,
			   struct tt_uuid *instance_uuid)
{
	struct memtx_sort_data *msd =
		memtx_sort_data_writer_new_empty(dirname, instance_uuid);

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

			/*
			 * We only read-view SKs that support recovering
			 * using sort data (the ones of TREE type).
			 */
			assert(space_rv->index_map[i]->def->type == TREE);

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
memtx_sort_data_writer_delete(struct memtx_sort_data *msd)
{
	if (msd == NULL)
		return; /* Nothing to do. */

	if (msd->fp != NULL)
		fclose(msd->fp);
	mh_memtx_sort_data_entries_delete(msd->entries);
	free(msd);
}

int
memtx_sort_data_writer_create_file(struct memtx_sort_data *msd,
				   struct vclock *vclock,
				   const char *filename)
{
	if (msd == NULL)
		return 0; /* Nothing to do. */

	assert(strlen(filename) < sizeof(msd->fname));
	snprintf(msd->fname, sizeof(msd->fname), "%s", filename);
	say_info("saving memtx sort data `%s'", msd->fname);

	/* Write the XLOG header. */
	struct xlog log;
	xlog_clear(&log);
	if (xdir_create_xlog(&msd->dir, &log, vclock) != 0)
		return -1;
	int fd;
	if (xlog_close_reuse_fd(&log, &fd, false) != 0) {
		xlog_discard(&log);
		return -1;
	}

	/*
	 * Continue as FILE so writes are buffered. The fdopen call does not
	 * truncate file on "w" but allows writes to arbitrary offsets in the
	 * file.
	 */
	msd->fp = fdopen(fd, "wb");
	if (msd->fp == NULL) {
		xlog_discard(&log);
		return -1;
	}

	/* The overall tuple count is to be updated later. */
	fprintf(msd->fp, "Cardinality: ");
	msd->cardinality_offset = ftell(msd->fp);
	if (msd->cardinality_offset == -1)
		return -1;
	fprintf(msd->fp, "%020lld\n", 0LL);

	/* Write all the sort data entries. */
	fprintf(msd->fp, "Entries: %u\n", mh_size(msd->entries));
	mh_int_t i;
	mh_foreach(msd->entries, i) {
		struct memtx_sort_data_entry *entry =
			mh_memtx_sort_data_entries_node(msd->entries, i);
		fprintf(msd->fp, "%u/%u: ", entry->key.space_id,
			entry->key.index_id);
		entry->offset_offset = ftell(msd->fp);
		if (entry->offset_offset == -1)
			return -1;
		fprintf(msd->fp, "%016lx, ", 0L);
		entry->psize_offset = ftell(msd->fp);
		if (entry->psize_offset == -1)
			return -1;
		fprintf(msd->fp, "%016lx, ", 0L);
		entry->len_offset = ftell(msd->fp);
		if (entry->len_offset == -1)
			return -1;
		fprintf(msd->fp, "%020ld\n", 0L);
	}
	fprintf(msd->fp, "\n");
	return 0;
}

int
memtx_sort_data_writer_close_file(struct memtx_sort_data *msd)
{
	if (msd == NULL)
		return 0; /* Nothing to do. */

	if (fseek(msd->fp, msd->cardinality_offset, SEEK_SET) != 0) {
		DIAG_SET(msd, "cardinality header member seek failed");
		return -1;
	}
	fprintf(msd->fp, "%020lld\n", (long long)msd->cardinality);
	fclose(msd->fp);
	msd->fp = NULL;
	say_info("done");
	return 0;
}

int
memtx_sort_data_writer_begin(struct memtx_sort_data *msd,
			     uint32_t space_id, uint32_t index_id,
			     bool *have_data)
{
	if (msd == NULL) {
		if (have_data != NULL)
			*have_data = false;
		return 0; /* Nothing to do. */
	}

	assert(msd->curr_entry == NULL);
	struct memtx_sort_data_key key = {space_id, index_id};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i == mh_end(msd->entries)) {
		if (have_data != NULL)
			*have_data = false; /* Not included index. */
		return 0;
	}

	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(msd->entries, i);
	assert(!entry->is_committed);
	if (fseek(msd->fp, 0, SEEK_END) != 0) {
		DIAG_SET(msd, "space %u: index #%u seek failed",
			 space_id, index_id);
		return -1;
	}
	entry->offset = ftell(msd->fp);
	if (entry->offset == -1) {
		DIAG_SET(msd, "space %u: index #%u ftell failed",
			 space_id, index_id);
		return -1;
	}
	entry->psize = 0; /* Just for clarity. */
	entry->len = 0; /* Ditto. */
	msd->curr_entry = entry;
	if (have_data != NULL)
		*have_data = true;
	return 0;
}

int
memtx_sort_data_writer_put(struct memtx_sort_data *msd,
			   void *data, size_t size, size_t count)
{
	if (msd == NULL)
		return 0; /* Nothing to do. */

	if (msd->curr_entry == NULL)
		return 0; /* No sort data from the PK. */

	assert(msd->curr_entry != NULL);
	if (fwrite(data, size, count, msd->fp) != count)
		return -1;
	msd->curr_entry->psize += size * count;
	msd->curr_entry->len += count;
	return 0;
}

int
memtx_sort_data_writer_commit(struct memtx_sort_data *msd)
{
	if (msd == NULL)
		return 0; /* Nothing to do. */

	if (msd->curr_entry == NULL)
		return 0; /* No sort data from the PK. */

	assert(msd->curr_entry != NULL);
	if (fseek(msd->fp, msd->curr_entry->offset_offset, SEEK_SET) != 0)
		return -1;
	fprintf(msd->fp, "%016lx, ", msd->curr_entry->offset);
	if (fseek(msd->fp, msd->curr_entry->psize_offset, SEEK_SET) != 0)
		return -1;
	fprintf(msd->fp, "%016lx, ", msd->curr_entry->psize);
	if (fseek(msd->fp, msd->curr_entry->len_offset, SEEK_SET) != 0)
		return -1;
	fprintf(msd->fp, "%020ld\n", msd->curr_entry->len);
	/* Only count PK tuples in cardinality. */
	if (msd->curr_entry->key.index_id == 0)
		msd->cardinality += msd->curr_entry->len;
	msd->curr_entry->is_committed = true;
	msd->curr_entry = NULL;
	return 0;
}

int
memtx_sort_data_writer_begin_pk(struct memtx_sort_data *msd, uint32_t space_id)
{
	return memtx_sort_data_writer_begin(msd, space_id, 0, NULL);
}

int
memtx_sort_data_writer_put_pk_tuple(struct memtx_sort_data *msd,
				    struct tuple *tuple)
{
	return memtx_sort_data_writer_put(msd, &tuple, sizeof(tuple), 1);
}

int
memtx_sort_data_writer_commit_pk(struct memtx_sort_data *msd)
{
	return memtx_sort_data_writer_commit(msd);
}

/* }}} */

/* {{{ Reader *****************************************************************/

/**
 * Parse an index sort data key entry part (a numeric value).
 *
 * @param fname - name of the sort data file (for logging);
 * @param line - the line the values are parsed in (for logging);
 * @param entry_name - the name of the key expected (for logging);
 * @param expect_after - the string to expect after the value (for logging);
 * @param base - base of the value;
 * @param entry_ptr - pointer to the value string;
 * @param after_ptr - pointer to the end of the value (output);
 * @param result - the parsed value (output).
 */
static bool
memtx_sort_data_parse_entry(const char *fname, const char *line,
			    const char *entry_name, const char *expect_after,
			    int base, const char *entry_ptr,
			    char **after_ptr, long *result)
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

static const char *
memtx_sort_data_parse_string(const char *line, const char *str)
{
	size_t str_len = strlen(str);
	if (memcmp(line, str, str_len) != 0)
		return NULL;
	return line + str_len;
}

struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *dirname, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid)
{
	struct memtx_sort_data_reader *msdr = xcalloc(1, sizeof(*msdr));
	msdr->msd = memtx_sort_data_writer_new_empty(dirname, instance_uuid);
	msdr->buffer_capacity = 1024 * 1024; /* In elements. */
	msdr->buffer = xcalloc(msdr->buffer_capacity, sizeof(*msdr->buffer));
	msdr->buffer_size = 0;
	msdr->buffer_i = 0;
	msdr->old2new = mh_ptrptr_new();

	struct memtx_sort_data *msd = msdr->msd;
	snprintf(msd->fname, sizeof(msd->fname), "%s", xdir_format_filename(
			&msd->dir, vclock_sum(vclock), NONE));

	/* Verify the XLOG header. */
	struct xlog_cursor cursor;
	if (xdir_open_cursor(&msd->dir, vclock_sum(vclock), &cursor) != 0)
		goto fail_free_no_file; /* No sort data file found. */

	/* Continue as FILE pointer to perform raw data reads. */
	int fd = cursor.fd;
	size_t xlog_header_size = cursor.meta.size;
	assert(xlog_header_size != SIZE_MAX);
	xlog_cursor_close(&cursor, true);
	msd->fp = fdopen(fd, "rb");
	if (msd->fp == NULL) {
		say_error("%s: fdopen failed", msd->fname);
		goto fail_free; /* Failed to open by fd. */
	}
	if (fseek(msd->fp, xlog_header_size, SEEK_SET) != 0) {
		say_error("%s: header skip failed", msd->fname);
		goto fail_close;
	}

	/* Get the sort data entries. */
	char tmp[256] = {};
	const char *key_cardinality = "Cardinality: ";
	const char *key_entries = "Entries: ";
	uint32_t read_entries = 0;
	while (fgets(tmp, sizeof(tmp), msd->fp)) {
		if (memcmp(tmp, key_cardinality,
			   strlen(key_cardinality)) == 0) {
			const char *cardinality_str = tmp +
						      strlen(key_cardinality);
			char *eol;
			msd->cardinality = strtol(cardinality_str, &eol, 10);
			if (*eol != '\n') {
				say_error("%s: invalid value for sort data "
					  "cardinality: %s", msd->fname, tmp);
				goto fail_close;
			}
			mh_ptrptr_reserve(msdr->old2new,
					  msd->cardinality, NULL);
		} else if (memcmp(tmp, key_entries, strlen(key_entries)) == 0) {
			const char *entry_count_str = tmp + strlen(key_entries);
			read_entries = atoi(entry_count_str);
		} else if (read_entries) {
			char *space_id_str = tmp;
			char *index_id_str;
			long space_id;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "space ID", "/",
							 10, space_id_str,
							 &index_id_str,
							 &space_id)) {
				goto fail_close;
			}

			char *offset_str;
			long index_id;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "index ID", ": ",
							 10, index_id_str,
							 &offset_str,
							 &index_id)) {
				goto fail_close;
			}

			char *psize_str;
			long offset;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "data offset", ", ",
							 16, offset_str,
							 &psize_str, &offset)) {
				goto fail_close;
			}

			char *len_str;
			long psize;
			if (!memtx_sort_data_parse_entry(msd->fname, tmp,
							 "physical size", ", ",
							 16, psize_str,
							 &len_str, &psize)) {
				goto fail_close;
			}

			char *end;
			long len = strtol(len_str, &end, 10);
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
	return msdr;

fail_close:
	fclose(msd->fp);
	msd->fp = NULL;
fail_free:
	say_warn("memtx sort data file `%s' ignored", msd->fname);
fail_free_no_file:
	memtx_sort_data_writer_delete(msd);
	mh_ptrptr_delete(msdr->old2new);
	free(msdr->buffer);
	free(msdr);
	return NULL;
}

int
memtx_sort_data_reader_pk_init(struct memtx_sort_data_reader *msdr,
			       uint32_t space_id)
{
	struct memtx_sort_data *msd = msdr->msd;
	struct memtx_sort_data_key key = {space_id, 0};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i == mh_end(msd->entries)) {
		msd->curr_entry = NULL;
		return 0;
	}

	/* Seek to the PK data and save the entry info to access later. */
	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(msd->entries, i);
	if (fseek(msd->fp, entry->offset, SEEK_SET) != 0) {
		DIAG_SET(msd, "space %u: PK seek failed", space_id);
		return -1;
	}
	msd->curr_entry = entry;
	return 0;
}

int
memtx_sort_data_reader_pk_add_tuple(struct memtx_sort_data_reader *msdr,
				    uint32_t space_id, bool is_first,
				    struct tuple *tuple)
{
	/* Read the sort data entry from the file on the first insert. */
	if (is_first && memtx_sort_data_reader_pk_init(msdr, space_id) != 0)
		return -1;

	struct memtx_sort_data *msd = msdr->msd;
	if (msd->curr_entry == NULL)
		return 0; /* No sort data for the space. */
	assert(msd->curr_entry->key.space_id == space_id);

	struct mh_ptrptr_node_t node;
	if (msdr->buffer_i >= msdr->buffer_size) {
		if (msd->curr_entry->len == 0) {
			DIAG_SET(msd, "space %u: PK length mismatch: "
				 "more PK tuples expected", space_id);
			return -1;
		}
		msdr->buffer_size = MIN(msdr->buffer_capacity,
					(size_t)msd->curr_entry->len);
		if (fread(msdr->buffer, sizeof(*msdr->buffer),
			  msdr->buffer_size, msd->fp) != msdr->buffer_size) {
			DIAG_SET(msd, "space %u: PK read failed", space_id);
			return -1;
		}
		msd->curr_entry->len -= msdr->buffer_size;
		msdr->buffer_i = 0;
	}
	node.key = msdr->buffer[msdr->buffer_i++];
	node.val = tuple;
	mh_ptrptr_put(msdr->old2new, &node, NULL, NULL);
	return 0;
}

int
memtx_sort_data_reader_pk_check(struct memtx_sort_data_reader *msdr,
				uint32_t space_id)
{
	struct memtx_sort_data *msd = msdr->msd;
	struct memtx_sort_data_key key = {space_id, 0};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i != mh_end(msd->entries)) {
		struct memtx_sort_data_entry *entry =
			mh_memtx_sort_data_entries_node(msd->entries, i);
		if (entry->len != 0) {
			DIAG_SET(msd, "space %u: PK length mismatch: not "
				 "all PK tuples had been used", space_id);
			return -1;
		}
	}
	return 0;
}

int
memtx_sort_data_reader_seek(struct memtx_sort_data_reader *msdr,
			    uint32_t space_id, uint32_t index_id,
			    bool *have_data)
{
	assert(have_data != NULL);
	assert(index_id != 0);

	struct memtx_sort_data *msd = msdr->msd;
	struct memtx_sort_data_key key = {space_id, index_id};
	mh_int_t i = mh_memtx_sort_data_entries_find(msd->entries, key, NULL);
	if (i == mh_end(msd->entries)) {
		*have_data = false; /* No SK sort data. */
		return 0;
	}

	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(msd->entries, i);
	if (fseek(msd->fp, entry->offset, SEEK_SET) != 0) {
		DIAG_SET(msd, "space %u: SK #%u not found", space_id, index_id);
		return -1;
	}
	msd->curr_entry = entry;
	*have_data = true;
	return 0;
}

size_t
memtx_sort_data_reader_get_size(struct memtx_sort_data_reader *msdr)
{
	struct memtx_sort_data *msd = msdr->msd;
	assert(msd->curr_entry != NULL); /* Only called if sort data exists. */
	return msd->curr_entry->psize;
}

int
memtx_sort_data_reader_get(struct memtx_sort_data_reader *msdr, void *buffer)
{
	struct memtx_sort_data *msd = msdr->msd;
	struct memtx_sort_data_entry *entry = msd->curr_entry;
	assert(entry != NULL); /* Only called if sort data exists. */
	if (fread(buffer, 1, entry->psize, msd->fp) != (size_t)entry->psize) {
		DIAG_SET(msd, "space %u: failed to read index #%u data",
			 entry->key.space_id, entry->key.index_id);
		return -1;
	}
	return 0;
}

struct tuple *
memtx_sort_data_reader_resolve_tuple(struct memtx_sort_data_reader *msdr,
				     struct tuple *old_ptr)
{
	struct memtx_sort_data *msd = msdr->msd;
	assert(msd->curr_entry != NULL); /* Only called if sort data exists. */
	mh_int_t i = mh_ptrptr_find(msdr->old2new, old_ptr, NULL);
	if (i == mh_end(msdr->old2new)) {
		DIAG_SET(msdr->msd, "space %u: tuple not found",
			 msd->curr_entry->key.space_id);
		return NULL;
	}
	return mh_ptrptr_node(msdr->old2new, i)->val;
}

void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *msdr)
{
	struct memtx_sort_data *msd = msdr->msd;
	fclose(msd->fp);
	msd->fp = NULL;
	memtx_sort_data_writer_delete(msd);
	mh_ptrptr_delete(msdr->old2new);
	free(msdr->buffer);
	free(msdr);
}

/* }}} */
