#include "xlog.h"
#include "memtx_sort_data.h"

#include "core/assoc.h"
#include "trivia/util.h"

#define DIAG_SET(writer_or_reader, ...) do { \
	diag_set(ClientError, ER_INVALID_SORTDATA_FILE, \
		 (writer_or_reader)->fname, tt_sprintf(__VA_ARGS__)); \
} while (false)

/**
 * Maps: (space_id, index_id) => (sort data entry information).
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
};

/** Same as memtx_sort_data_entry but with additional writer context. */
struct memtx_sort_data_writer_entry {
	/** Basic entry information. */
	struct memtx_sort_data_entry base;
	/** Offset of offset field in the file header. */
	long offset_offset;
	/** Offset of psize field in the file header. */
	long psize_offset;
	/** Offset of len field in the file header. */
	long len_offset;
	/** The data is fully written into the file already. */
	bool is_committed;
};

#define mh_name _memtx_sort_data_entries
#define mh_key_t struct memtx_sort_data_key
#define mh_node_t struct memtx_sort_data_entry
#define mh_arg_t void *
#define mh_hash(a, _) (memtx_sort_data_key_hash((a)->key))
#define mh_hash_key(a, _) (memtx_sort_data_key_hash(a))
#define mh_cmp(a, b, _) (memtx_sort_data_key_cmp((a)->key, (b)->key))
#define mh_cmp_key(a, b, _) (memtx_sort_data_key_cmp(a, (b)->key))
#define MH_SOURCE
#include "salad/mhash.h"

#define mh_name _memtx_sort_data_writer_entries
#define mh_key_t struct memtx_sort_data_key
#define mh_node_t struct memtx_sort_data_writer_entry
#define mh_arg_t void *
#define mh_hash(a, _) (memtx_sort_data_key_hash((a)->base.key))
#define mh_hash_key(a, _) (memtx_sort_data_key_hash(a))
#define mh_cmp(a, b, _) (memtx_sort_data_key_cmp((a)->base.key, (b)->base.key))
#define mh_cmp_key(a, b, _) (memtx_sort_data_key_cmp(a, (b)->base.key))
#define MH_SOURCE
#include "salad/mhash.h"

/** The sort data file writer context. */
struct memtx_sort_data_writer {
	/** The sort data file pointer. */
	FILE *fp;
	/** The sort data file name. */
	char fname[PATH_MAX];
	/** The information about the sort data entries. */
	struct mh_memtx_sort_data_writer_entries_t *entries;
	/** The currently handled sort data entry. */
	struct memtx_sort_data_writer_entry *curr_entry;
	/** The total amount of tuples in saved spaces. */
	size_t cardinality;
	/** The offset to the "Cardinality" file header key. */
	long cardinality_offset;
};

/** The sort data reader context. */
struct memtx_sort_data_reader {
	/** The sort data file pointer. */
	FILE *fp;
	/** The sort data file name. */
	char fname[PATH_MAX];
	/** The information about the sort data entries. */
	struct mh_memtx_sort_data_entries_t *entries;
	/** The currently handled sort data entry. */
	struct memtx_sort_data_entry *curr_entry;
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

/* {{{ Writer *****************************************************************/

struct memtx_sort_data_writer *
memtx_sort_data_writer_new(struct read_view *rv)
{
	struct memtx_sort_data_writer *writer = xmalloc(sizeof(*writer));
	writer->fp = NULL;
	memset(writer->fname, 0, sizeof(writer->fname));
	writer->entries = mh_memtx_sort_data_writer_entries_new();
	writer->curr_entry = NULL;
	writer->cardinality = 0;
	writer->cardinality_offset = 0;

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
			struct memtx_sort_data_writer_entry entry = {};
			entry.base.key.space_id = space_rv->id;
			entry.base.key.index_id = i;
			mh_memtx_sort_data_writer_entries_put(writer->entries,
							      &entry, NULL,
							      NULL);
			need_sort_data = true;
		}

		/* Insert the PK entry if required. */
		if (need_sort_data) {
			struct memtx_sort_data_writer_entry entry = {};
			entry.base.key.space_id = space_rv->id;
			entry.base.key.index_id = 0; /* Just for clarity. */
			mh_memtx_sort_data_writer_entries_put(writer->entries,
							      &entry, NULL,
							      NULL);
		}
	}
	return writer;
}

void
memtx_sort_data_writer_delete(struct memtx_sort_data_writer *writer)
{
	if (writer->fp != NULL)
		fclose(writer->fp);
	mh_memtx_sort_data_writer_entries_delete(writer->entries);
	free(writer);
}

int
memtx_sort_data_writer_create_file(struct memtx_sort_data_writer *writer,
				   const char *filename,
				   const struct tt_uuid *instance_uuid,
				   struct vclock *vclock)
{
	assert(strlen(filename) < sizeof(writer->fname));
	snprintf(writer->fname, sizeof(writer->fname), "%s", filename);
	say_info("saving memtx sort data `%s'", writer->fname);

	/* The file existence is checked in the xdir_create_paired_xlog. */
	assert(access(writer->fname, F_OK) != 0);

	/* Open the file for write. */
	writer->fp = fopen(writer->fname, "wb");
	if (writer->fp == NULL) {
		DIAG_SET(writer, "failed to open the file for write");
		return -1;
	}

	/* Write the file header. */
	fprintf(writer->fp, "SORTDATA\n1\n");
	fprintf(writer->fp, "Version: %s\n", PACKAGE_VERSION);
	fprintf(writer->fp, "Instance: %s\n", tt_uuid_str(instance_uuid));
	fprintf(writer->fp, "VClock: %s\n\n", vclock_to_string(vclock));

	/* The overall tuple count is to be updated later. */
	fprintf(writer->fp, "Cardinality: ");
	writer->cardinality_offset = ftell(writer->fp);
	if (writer->cardinality_offset == -1)
		return -1;
	fprintf(writer->fp, "%020lld\n", 0LL);

	/* Write all the sort data entries. */
	fprintf(writer->fp, "Entries: %u\n", mh_size(writer->entries));
	mh_int_t i;
	mh_foreach(writer->entries, i) {
		struct memtx_sort_data_writer_entry *entry =
			mh_memtx_sort_data_writer_entries_node(
				writer->entries, i);
		fprintf(writer->fp, "%u/%u: ", entry->base.key.space_id,
			entry->base.key.index_id);
		entry->offset_offset = ftell(writer->fp);
		if (entry->offset_offset == -1)
			return -1;
		fprintf(writer->fp, "%016lx, ", 0L);
		entry->psize_offset = ftell(writer->fp);
		if (entry->psize_offset == -1)
			return -1;
		fprintf(writer->fp, "%016lx, ", 0L);
		entry->len_offset = ftell(writer->fp);
		if (entry->len_offset == -1)
			return -1;
		fprintf(writer->fp, "%020ld\n", 0L);
	}
	fprintf(writer->fp, "\n");
	return 0;
}

int
memtx_sort_data_writer_close_file(struct memtx_sort_data_writer *writer)
{
	if (fseek(writer->fp, writer->cardinality_offset, SEEK_SET) != 0) {
		DIAG_SET(writer, "cardinality header member seek failed");
		return -1;
	}
	fprintf(writer->fp, "%020lld\n", (long long)writer->cardinality);
	fclose(writer->fp);
	writer->fp = NULL;
	say_info("done");
	return 0;
}

int
memtx_sort_data_writer_begin(struct memtx_sort_data_writer *writer,
			     uint32_t space_id, uint32_t index_id,
			     bool *have_data)
{
	/* The `have_data' param is only NULL for a PK. */
	assert((index_id == 0) == (have_data == NULL));
	assert(writer->curr_entry == NULL);
	struct memtx_sort_data_key key = {space_id, index_id};
	mh_int_t i = mh_memtx_sort_data_writer_entries_find(writer->entries,
							    key, NULL);
	if (i == mh_end(writer->entries)) {
		if (have_data != NULL)
			*have_data = false; /* Not included index. */
		return 0;
	}

	struct memtx_sort_data_writer_entry *entry =
		mh_memtx_sort_data_writer_entries_node(writer->entries, i);
	assert(!entry->is_committed);
	if (fseek(writer->fp, 0, SEEK_END) != 0) {
		DIAG_SET(writer, "space %u: index #%u seek failed",
			 space_id, index_id);
		return -1;
	}
	entry->base.offset = ftell(writer->fp);
	if (entry->base.offset == -1) {
		DIAG_SET(writer, "space %u: index #%u ftell failed",
			 space_id, index_id);
		return -1;
	}
	entry->base.psize = 0; /* Just for clarity. */
	entry->base.len = 0; /* Ditto. */
	writer->curr_entry = entry;
	if (have_data != NULL)
		*have_data = true;
	return 0;
}

int
memtx_sort_data_writer_put(struct memtx_sort_data_writer *writer,
			   void *data, size_t size, size_t count)
{
	if (writer->curr_entry == NULL)
		return 0; /* No sort data from the PK. */

	assert(writer->curr_entry != NULL);
	if (fwrite(data, size, count, writer->fp) != count)
		return -1;
	writer->curr_entry->base.psize += size * count;
	writer->curr_entry->base.len += count;
	return 0;
}

int
memtx_sort_data_writer_commit(struct memtx_sort_data_writer *writer)
{
	if (writer->curr_entry == NULL)
		return 0; /* No sort data from the PK. */

	assert(writer->curr_entry != NULL);
	if (fseek(writer->fp, writer->curr_entry->offset_offset, SEEK_SET) != 0)
		return -1;
	fprintf(writer->fp, "%016lx, ", writer->curr_entry->base.offset);
	if (fseek(writer->fp, writer->curr_entry->psize_offset, SEEK_SET) != 0)
		return -1;
	fprintf(writer->fp, "%016lx, ", writer->curr_entry->base.psize);
	if (fseek(writer->fp, writer->curr_entry->len_offset, SEEK_SET) != 0)
		return -1;
	fprintf(writer->fp, "%020ld\n", writer->curr_entry->base.len);
	/* Only count PK tuples in cardinality. */
	if (writer->curr_entry->base.key.index_id == 0)
		writer->cardinality += writer->curr_entry->base.len;
	writer->curr_entry->is_committed = true;
	writer->curr_entry = NULL;
	return 0;
}

int
memtx_sort_data_writer_begin_pk(struct memtx_sort_data_writer *writer, uint32_t space_id)
{
	return memtx_sort_data_writer_begin(writer, space_id, 0, NULL);
}

int
memtx_sort_data_writer_put_pk_tuple(struct memtx_sort_data_writer *writer,
				    struct tuple *tuple)
{
	return memtx_sort_data_writer_put(writer, &tuple, sizeof(tuple), 1);
}

int
memtx_sort_data_writer_commit_pk(struct memtx_sort_data_writer *writer)
{
	return memtx_sort_data_writer_commit(writer);
}

/* }}} */

/* {{{ Reader *****************************************************************/

/*
 * Read the expected next sort data file metata key. Returns the key value.
 */
static char *
memtx_sort_data_read_meta(struct memtx_sort_data_reader *reader,
			  const char *expect, const char *pretty_name)
{
	static char line[256];
	if (fgets(line, sizeof(line), reader->fp) == NULL) {
		say_error("%s: %s read failed", reader->fname, pretty_name);
		return NULL;
	}
	size_t expect_len = strlen(expect);
	if (memcmp(line, expect, expect_len) != 0) {
		say_error("%s: %s expected, got: %s",
			  reader->fname, pretty_name, line);
		return NULL;
	}
	return line + expect_len;
}

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
memtx_sort_data_parse_value(const char *fname, const char *line,
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

/**
 * Read the next sort data entry.
 */
static int
memtx_sort_data_read_entry(struct memtx_sort_data_reader *reader,
			   struct memtx_sort_data_entry *entry)
{
	/* Read the entry. */
	static char line[256];
	if (fgets(line, sizeof(line), reader->fp) == NULL) {
		say_error("%s: entry read failed", reader->fname);
		return -1;
	}

	/* Parse the space ID. */
	char *space_id_str = line;
	char *index_id_str;
	long space_id;
	if (!memtx_sort_data_parse_value(reader->fname, line, "space ID",
					 "/", 10, space_id_str,
					 &index_id_str, &space_id))
		return -1;

	/* Parse the index ID. */
	char *offset_str;
	long index_id;
	if (!memtx_sort_data_parse_value(reader->fname, line, "index ID",
					 ": ", 10, index_id_str,
					 &offset_str, &index_id))
		return -1;

	/* Parse the sort data offset. */
	char *psize_str;
	long offset;
	if (!memtx_sort_data_parse_value(reader->fname, line,
					 "data offset", ", ",
					 16, offset_str,
					 &psize_str, &offset))
		return -1;

	/* Parse the sort data phisical size. */
	char *len_str;
	long psize;
	if (!memtx_sort_data_parse_value(reader->fname, line,
					 "physical size", ", ",
					 16, psize_str,
					 &len_str, &psize))
		return -1;

	/* Parse the sort data logical size (tuple count). */
	char *end;
	long len = strtol(len_str, &end, 10);
	if (*end != '\n') {
		say_warn("%s: unexpected contents in the sort"
			 " data key: %s", reader->fname, line);
		return -1;
	}

	/* Sanity check. */
	if ((len == 0) != (psize == 0)) {
		say_error("%s: entry size verification failed", reader->fname);
		return -1;
	}

	entry->key.space_id = space_id;
	entry->key.index_id = index_id;
	entry->offset = offset;
	entry->psize = psize;
	entry->len = len;
	return 0;
}

struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *filename, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid)
{
	struct memtx_sort_data_reader *reader = xmalloc(sizeof(*reader));
	snprintf(reader->fname, sizeof(reader->fname), "%s", filename);
	reader->entries = mh_memtx_sort_data_entries_new();
	reader->curr_entry = NULL;
	reader->buffer_capacity = 1024 * 1024; /* In elements. */
	reader->buffer = xmalloc(reader->buffer_capacity *
				 sizeof(*reader->buffer));
	reader->buffer_size = 0;
	reader->buffer_i = 0;
	reader->old2new = mh_ptrptr_new();

	/* Open the .sortdata file. */
	reader->fp = fopen(reader->fname, "rb");
	if (reader->fp == NULL) {
		say_error("%s: file open failed", reader->fname);
		goto fail_free;
	}

	/* Verify the file magic. */
	if (memtx_sort_data_read_meta(reader, "SORTDATA\n",
				      "file magic") == NULL)
		goto fail_close;

	/* Verify the file version. */
	if (memtx_sort_data_read_meta(reader, "1\n",
				      "file version") == NULL)
		goto fail_close;

	/* Skip the Tarantool version. */
	if (memtx_sort_data_read_meta(reader, "Version: ",
				      "Tarantool version") == NULL)
		goto fail_close;

	/* Verify the instance UUID. */
	char *uuid_str = memtx_sort_data_read_meta(reader, "Instance: ",
						   "instance UUID");
	if (uuid_str == NULL)
		goto fail_close;
	if (strlen(uuid_str) - 1 /* Newline. */ != UUID_STR_LEN) {
		say_error("%s: invalid UUID length: %s",
			  reader->fname, uuid_str);
		goto fail_close;
	}
	uuid_str[UUID_STR_LEN] = '\0';
	struct tt_uuid sortdata_uuid;
	if (tt_uuid_from_string(uuid_str, &sortdata_uuid) != 0) {
		say_error("%s: invalid UUID: %s", reader->fname, uuid_str);
		goto fail_close;
	}
	if (!tt_uuid_is_equal(&sortdata_uuid, instance_uuid)) {
		say_error("%s: unmatched UUID: %s", reader->fname, uuid_str);
		goto fail_close;
	}

	/* Verify the vclock signature. */
	char *vclock_str =
		memtx_sort_data_read_meta(reader, "VClock: ", "VClock");
	if (vclock_str == NULL)
		goto fail_close;
	vclock_str[strlen(vclock_str) - 1] = '\0'; /* Drop the newline. */
	struct vclock sortdata_vclock = {};
	if (vclock_from_string(&sortdata_vclock, vclock_str) != 0) {
		say_error("%s: invalid VClock: %s", reader->fname, vclock_str);
		goto fail_close;
	}
	long long sortdata_signature = vclock_sum(&sortdata_vclock);
	long long snapshot_signature = vclock_sum(vclock);
	if (sortdata_signature != snapshot_signature) {
		say_error("%s: unmatched VClock: %s (%lld != %lld)",
			  reader->fname, vclock_str,
			  sortdata_signature, snapshot_signature);
		goto fail_close;
	}

	/* Skip the newline. */
	if (memtx_sort_data_read_meta(reader, "\n", "newline") == NULL)
		goto fail_close;

	/* Get the sort data file cardinality. */
	char *cardinality_str = memtx_sort_data_read_meta(reader,
							  "Cardinality: ",
							  "cardinality");
	if (cardinality_str == NULL)
		goto fail_close;
	char *eol;
	long cardinality = strtol(cardinality_str, &eol, 10);
	if (*eol != '\n') {
		say_error("%s: invalid cardinality: %s",
			  reader->fname, cardinality_str);
		goto fail_close;
	}
	mh_ptrptr_reserve(reader->old2new, cardinality, NULL);

	/* Get the sort data entry count. */
	char *entries_str =
		memtx_sort_data_read_meta(reader, "Entries: ", "entry count");
	if (entries_str == NULL)
		goto fail_close;
	long read_entries = strtol(entries_str, &eol, 10);
	if (*eol != '\n') {
		say_error("%s: invalid entry count: %s",
			  reader->fname, entries_str);
		goto fail_close;
	}

	/* Get the sort data entries. */
	for (long i = 0; i < read_entries; i++) {
		struct memtx_sort_data_entry entry;
		if (memtx_sort_data_read_entry(reader, &entry) != 0)
			goto fail_close;
		mh_memtx_sort_data_entries_put(reader->entries,
					       &entry, NULL, NULL);
	}

	/* Skip the newline after the file header. */
	if (memtx_sort_data_read_meta(reader, "\n", "last newline") == NULL)
		goto fail_close;

	say_info("using the memtx sort data from `%s'", reader->fname);
	return reader;

fail_close:
	fclose(reader->fp);
	say_warn("memtx sort data file `%s' ignored", reader->fname);
fail_free:
	mh_memtx_sort_data_entries_delete(reader->entries);
	mh_ptrptr_delete(reader->old2new);
	free(reader->buffer);
	free(reader);
	return NULL;
}

int
memtx_sort_data_reader_pk_init(struct memtx_sort_data_reader *reader,
			       uint32_t space_id)
{
	struct memtx_sort_data_key key = {space_id, 0};
	mh_int_t i = mh_memtx_sort_data_entries_find(reader->entries,
						     key, NULL);
	if (i == mh_end(reader->entries)) {
		reader->curr_entry = NULL;
		return 0;
	}

	/* Seek to the PK data and save the entry info to access later. */
	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(reader->entries, i);
	if (fseek(reader->fp, entry->offset, SEEK_SET) != 0) {
		DIAG_SET(reader, "space %u: PK seek failed", space_id);
		return -1;
	}
	reader->curr_entry = entry;
	return 0;
}

int
memtx_sort_data_reader_pk_add_tuple(struct memtx_sort_data_reader *reader,
				    uint32_t space_id, bool is_first,
				    struct tuple *tuple)
{
	/* Read the sort data entry from the file on the first insert. */
	if (is_first && memtx_sort_data_reader_pk_init(reader, space_id) != 0)
		return -1;

	if (reader->curr_entry == NULL)
		return 0; /* No sort data for the space. */
	assert(reader->curr_entry->key.space_id == space_id);

	struct mh_ptrptr_node_t node;
	if (reader->buffer_i >= reader->buffer_size) {
		if (reader->curr_entry->len == 0) {
			DIAG_SET(reader, "space %u: PK length mismatch: "
				 "more PK tuples expected", space_id);
			return -1;
		}
		reader->buffer_size = MIN(reader->buffer_capacity,
					  (size_t)reader->curr_entry->len);
		if (fread(reader->buffer,
			  sizeof(*reader->buffer),
			  reader->buffer_size,
			  reader->fp) != reader->buffer_size) {
			DIAG_SET(reader, "space %u: PK read failed", space_id);
			return -1;
		}
		reader->curr_entry->len -= reader->buffer_size;
		reader->buffer_i = 0;
	}
	node.key = reader->buffer[reader->buffer_i++];
	node.val = tuple;
	mh_ptrptr_put(reader->old2new, &node, NULL, NULL);
	return 0;
}

int
memtx_sort_data_reader_pk_check(struct memtx_sort_data_reader *reader,
				uint32_t space_id)
{
	struct memtx_sort_data_key key = {space_id, 0};
	mh_int_t i = mh_memtx_sort_data_entries_find(reader->entries,
						     key, NULL);
	if (i != mh_end(reader->entries)) {
		struct memtx_sort_data_entry *entry =
			mh_memtx_sort_data_entries_node(reader->entries, i);
		if (entry->len != 0) {
			DIAG_SET(reader, "space %u: PK length mismatch: not "
				 "all PK tuples had been used", space_id);
			return -1;
		}
	}
	return 0;
}

int
memtx_sort_data_reader_seek(struct memtx_sort_data_reader *reader,
			    uint32_t space_id, uint32_t index_id,
			    bool *have_data)
{
	assert(have_data != NULL);
	assert(index_id != 0);

	struct memtx_sort_data_key key = {space_id, index_id};
	mh_int_t i = mh_memtx_sort_data_entries_find(reader->entries,
						     key, NULL);
	if (i == mh_end(reader->entries)) {
		*have_data = false; /* No SK sort data. */
		return 0;
	}

	struct memtx_sort_data_entry *entry =
		mh_memtx_sort_data_entries_node(reader->entries, i);
	if (fseek(reader->fp, entry->offset, SEEK_SET) != 0) {
		DIAG_SET(reader, "space %u: index #%u not found",
			 space_id, index_id);
		return -1;
	}
	reader->curr_entry = entry;
	*have_data = true;
	return 0;
}

size_t
memtx_sort_data_reader_get_size(struct memtx_sort_data_reader *reader)
{
	/* Only called if sort data exists. */
	assert(reader->curr_entry != NULL);
	return reader->curr_entry->psize;
}

int
memtx_sort_data_reader_get(struct memtx_sort_data_reader *reader, void *buffer)
{
	struct memtx_sort_data_entry *entry = reader->curr_entry;
	assert(entry != NULL); /* Only called if sort data exists. */
	if (fread(buffer, 1, entry->psize,
		  reader->fp) != (size_t)entry->psize) {
		DIAG_SET(reader, "space %u: failed to read index #%u data",
			 entry->key.space_id, entry->key.index_id);
		return -1;
	}
	return 0;
}

struct tuple *
memtx_sort_data_reader_resolve_tuple(struct memtx_sort_data_reader *reader,
				     struct tuple *old_ptr)
{
	/* Only called if sort data exists. */
	assert(reader->curr_entry != NULL);
	mh_int_t i = mh_ptrptr_find(reader->old2new, old_ptr, NULL);
	if (i == mh_end(reader->old2new)) {
		DIAG_SET(reader, "space %u: tuple not found",
			 reader->curr_entry->key.space_id);
		return NULL;
	}
	return mh_ptrptr_node(reader->old2new, i)->val;
}

void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *reader)
{
	fclose(reader->fp);
	mh_memtx_sort_data_entries_delete(reader->entries);
	mh_ptrptr_delete(reader->old2new);
	free(reader->buffer);
	free(reader);
}

/* }}} */
