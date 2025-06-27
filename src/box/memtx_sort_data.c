#include "xlog.h"
#include "memtx_sort_data.h"

#include "core/assoc.h"
#include "trivia/util.h"

#define DIAG_SET(writer_or_reader, ...) do { \
	diag_set(ClientError, ER_INVALID_SORTDATA_FILE, \
		 (writer_or_reader)->filename, tt_sprintf(__VA_ARGS__)); \
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

/** The sort data file header entry (reader version). */
struct memtx_sort_data_reader_entry {
	/** The entry identifier. */
	struct memtx_sort_data_key key;
	/** The offset of the sort data in the file. */
	long offset;
	/** The physical size of the sort data. */
	long psize;
};

/** The sort data file header entry (writer version). */
struct memtx_sort_data_writer_entry {
	/** Basic entry information. */
	struct memtx_sort_data_key key;
	/** Offset of the corresponding entry in the file header. */
	long header_entry_offset;
	/** The offset of the sort data in the file. */
	long offset;
	/** The physical size of the sort data. */
	long psize;
	/** The amount of tuples stored. */
	long len;
};

#define mh_name _memtx_sort_data_entries
#define mh_key_t struct memtx_sort_data_key
#define mh_node_t struct memtx_sort_data_reader_entry
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
#define mh_hash(a, _) (memtx_sort_data_key_hash((a)->key))
#define mh_hash_key(a, _) (memtx_sort_data_key_hash(a))
#define mh_cmp(a, b, _) (memtx_sort_data_key_cmp((a)->key, (b)->key))
#define mh_cmp_key(a, b, _) (memtx_sort_data_key_cmp(a, (b)->key))
#define MH_SOURCE
#include "salad/mhash.h"

/** The sort data file writer context. */
struct memtx_sort_data_writer {
	/** The sort data file pointer. */
	FILE *fp;
	/** The sort data file name. */
	char filename[PATH_MAX];
	/** The information about the sort data entries. */
	struct mh_memtx_sort_data_writer_entries_t *entries;
	/** The currently handled sort data entry. */
	struct memtx_sort_data_writer_entry *curr_entry;
	/** The total amount of tuples in saved spaces. */
	size_t cardinality;
	/** The offset to the "Cardinality" file header key. */
	long cardinality_offset;
	/** The offset to the next index entry in the file header. */
	long next_header_entry_offset;
};

/** The sort data reader context. */
struct memtx_sort_data_reader {
	/** The sort data file pointer. */
	FILE *fp;
	/** The sort data file name. */
	char filename[PATH_MAX];
	/** The information about the sort data entries. */
	struct mh_memtx_sort_data_entries_t *entries;
	/** The currently handled sort data entry, NULL if space is skipped. */
	struct memtx_sort_data_reader_entry *curr_entry;
	/** The currently handled space ID. */
	uint32_t curr_space_id;
	/** The old to new tuple address map used on recovery. */
	struct mh_ptrptr_t *old2new;
	/** The buffer to use on fread. */
	void *buffer;
};

/* {{{ Utilities **************************************************************/

/** Sort data file header format strings. */
static const char *CARDINALITY_FMT = "Cardinality: %020lld\n";
static const char *ENTRIES_FMT = "Entries: %010u\n";
static const char *ENTRY_FMT = "%010u/%010u: %016lx, %016lx, %20li\n";

/**
 * Returns the sort data file name based on the corresponding @a snap_filename.
 */
static const char *
memtx_sort_data_filename(const char *snap_filename)
{
	char *snap_ext = strrchr(snap_filename, '.');
	assert(snap_ext != NULL);
	if (strcmp(snap_ext, ".inprogress") == 0) {
		snap_ext -= strlen(".snap");
		assert(snap_ext > snap_filename);
	}
	assert(strcmp(snap_ext, ".snap") == 0 ||
	       strcmp(snap_ext, ".snap.inprogress") == 0);
	int snap_filename_noext_len = snap_ext - snap_filename;
	return tt_snprintf(PATH_MAX, "%.*s.sortdata",
			   snap_filename_noext_len, snap_filename);
}

/** Seek to the specified offset in the sort data file. */
static int
seek(struct memtx_sort_data_writer *writer, long offset, int whence)
{
	if (fseek(writer->fp, offset, whence) != 0) {
		DIAG_SET(writer, "failed to fseek in the sort data file");
		return -1;
	}
	return 0;
}

/** Get the current sort data file offset. */
static int
get_offset(struct memtx_sort_data_writer *writer, long *offset)
{
	*offset = ftell(writer->fp);
	if (*offset == -1) {
		DIAG_SET(writer, "failed to ftell on the sort data file");
		return -1;
	}
	return 0;
}

/** Write the formatted string into the sort data file. */
static int
print(struct memtx_sort_data_writer *writer, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	if (vfprintf(writer->fp, fmt, args) < 0) {
		diag_set(SystemError, "%s: failed to write the sort data file",
			 writer->filename);
		return -1;
	}
	va_end(args);
	return 0;
}

/* }}} */

/* {{{ Writer *****************************************************************/

struct memtx_sort_data_writer *
memtx_sort_data_writer_new(void)
{
	struct memtx_sort_data_writer *writer = xmalloc(sizeof(*writer));
	writer->fp = NULL;
	memset(writer->filename, 0, sizeof(writer->filename));
	writer->entries = mh_memtx_sort_data_writer_entries_new();
	writer->curr_entry = NULL;
	writer->cardinality = 0;
	writer->cardinality_offset = 0;
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
				   const char *snap_filename,
				   struct vclock *vclock,
				   const struct tt_uuid *instance_uuid,
				   struct read_view *rv)
{
	/* Check a materialized file does not exist already. */
	const char *filename = memtx_sort_data_filename(snap_filename);
	if (access(filename, F_OK) == 0) {
		errno = EEXIST;
		diag_set(SystemError, "sort data file '%s'", filename);
		return -1;
	}

	/* The in-progress file name. */
	VERIFY((size_t)snprintf(writer->filename, sizeof(writer->filename),
				"%s.inprogress", filename) <
	       sizeof(writer->filename));

	/* Open the file for write. */
	say_info("saving memtx sort data `%s'", writer->filename);
	writer->fp = fopen(writer->filename, "wb");
	if (writer->fp == NULL) {
		DIAG_SET(writer, "failed to open the file for write");
		return -1;
	}

	/* Write the file header. */
	if (print(writer,
		  "SORTDATA\n"
		  "1\n"
		  "Version: %s\n"
		  "Instance: %s\n"
		  "VClock: %s\n\n",
		  PACKAGE_VERSION,
		  tt_uuid_str(instance_uuid),
		  vclock_to_string(vclock)) != 0)
		return -1;

	/* The overall tuple count is to be updated later. */
	if (get_offset(writer, &writer->cardinality_offset) != 0 ||
	    print(writer, CARDINALITY_FMT, 0) != 0)
		return -1;

	/* Write the entry count to fill it later. */
	long entries_offset;
	if (get_offset(writer, &entries_offset) != 0 ||
	    print(writer, ENTRIES_FMT, 0) != 0)
		return -1;

	/* Save the pointer to the first sort data header entry. */
	if (get_offset(writer, &writer->next_header_entry_offset) != 0)
		return -1;

	/* Write dummy header entries to fill them later. */
	uint32_t entry_count = 0;
	struct space_read_view *space_rv;
	read_view_foreach_space(space_rv, rv) {
		if (space_rv->index_count <= 1)
			continue;
		for (uint32_t i = 0; i < space_rv->index_count; i++) {
			print(writer, ENTRY_FMT, 0, 0, 0L, 0L, 0L);
			entry_count++;
		}
	}

	/* The final newline. */
	if (print(writer, "\n") != 0)
		return -1;

	/* Write the calculated entry count and return. */
	if (seek(writer, entries_offset, SEEK_SET) != 0)
		return -1;
	return print(writer, ENTRIES_FMT, entry_count);
}

int
memtx_sort_data_writer_close_file(struct memtx_sort_data_writer *writer)
{
	if (seek(writer, writer->cardinality_offset, SEEK_SET) != 0 ||
	    print(writer, CARDINALITY_FMT, (long long)writer->cardinality) != 0)
		return -1;
	fclose(writer->fp);
	writer->fp = NULL;
	say_info("done");
	return 0;
}

int
memtx_sort_data_writer_begin(struct memtx_sort_data_writer *writer,
			     uint32_t space_id, uint32_t index_id)
{
	/* The new index sort data is to be written at the end of the file. */
	if (seek(writer, 0, SEEK_END) != 0)
		return -1;
	long file_end_offset;
	if (get_offset(writer, &file_end_offset) != 0)
		return -1;

	assert(writer->curr_entry == NULL);
	struct memtx_sort_data_writer_entry entry = {};
	entry.key.space_id = space_id;
	entry.key.index_id = index_id;
	entry.offset = file_end_offset;
	entry.header_entry_offset = writer->next_header_entry_offset;
	mh_int_t i = mh_memtx_sort_data_writer_entries_put(writer->entries,
							   &entry, NULL, NULL);
	writer->next_header_entry_offset += strlen(tt_sprintf(ENTRY_FMT, 0,
							      0, 0L, 0L, 0L));
	writer->curr_entry =
		mh_memtx_sort_data_writer_entries_node(writer->entries, i);
	return 0;
}

int
memtx_sort_data_writer_put(struct memtx_sort_data_writer *writer,
			   void *data, size_t size, size_t count)
{
	assert(writer->curr_entry != NULL);
	if (fwrite(data, size, count, writer->fp) != count) {
		DIAG_SET(writer, "failed to write the sort data");
		return -1;
	}
	writer->curr_entry->psize += size * count;
	writer->curr_entry->len += count;

	/* Count PK tuples for the sort data file cardinality. */
	if (writer->curr_entry->key.index_id == 0)
		writer->cardinality += count;
	return 0;
}

int
memtx_sort_data_writer_commit(struct memtx_sort_data_writer *writer)
{
	/* Update the dummy sort data entry in the file header. */
	assert(writer->curr_entry != NULL);
	struct memtx_sort_data_writer_entry *entry = writer->curr_entry;
	if (seek(writer, entry->header_entry_offset, SEEK_SET) != 0 ||
	    print(writer, ENTRY_FMT, entry->key.space_id, entry->key.index_id,
		  entry->offset, entry->psize, entry->len) != 0)
		return -1;
	writer->curr_entry = NULL;
	return 0;
}

int
memtx_sort_data_writer_begin_pk(struct memtx_sort_data_writer *writer,
				uint32_t space_id)
{
	return memtx_sort_data_writer_begin(writer, space_id, 0);
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

int
memtx_sort_data_writer_materialize(struct memtx_sort_data_writer *writer)
{
	/* The file must be successfully created. */
	assert(strlen(writer->filename) != 0);

	char *filename = writer->filename;
	char new_filename[PATH_MAX];
	char *suffix = strrchr(filename, '.');

	assert(suffix);
	assert(strcmp(suffix, ".inprogress") == 0);

	/* Create a new filename without '.inprogress' suffix. */
	memcpy(new_filename, filename, suffix - filename);
	new_filename[suffix - filename] = '\0';

	if (rename(filename, new_filename) != 0) {
		say_syserror("can't rename %s to %s", filename, new_filename);
		diag_set(SystemError, "failed to rename '%s' file", filename);
		return -1;
	}
	filename[suffix - filename] = '\0';
	return 0;
}

void
memtx_sort_data_writer_discard(struct memtx_sort_data_writer *writer)
{
	/* Check if it's created. */
	if (strlen(writer->filename) != 0)
		xlog_remove_file(writer->filename, 0, NULL);
}

/* }}} */

/* {{{ Reader *****************************************************************/

/**
 * Read the expected next contents in the file header. Returns a pointer to the
 * string right after the expected one (the buffer is statically allocated).
 *
 * WARNING: the max size of the header line is limited.
 */
static char *
memtx_sort_data_header_expect(struct memtx_sort_data_reader *reader,
			      const char *expect)
{
	char *line = tt_static_buf();
	size_t expect_len = strlen(expect);
	if (fgets(line, TT_STATIC_BUF_LEN, reader->fp) == NULL ||
	    memcmp(line, expect, expect_len) != 0) {
		say_error("%s: file header read failed", reader->filename);
		return NULL;
	}
	return line + expect_len;
}

struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *snap_filename,
			   const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid)
{
	const char *filename = memtx_sort_data_filename(snap_filename);
	struct memtx_sort_data_reader *reader = xmalloc(sizeof(*reader));
	snprintf(reader->filename, sizeof(reader->filename), "%s", filename);
	reader->entries = mh_memtx_sort_data_entries_new();
	reader->curr_entry = NULL;
	reader->curr_space_id = BOX_ID_NIL;
	reader->old2new = mh_ptrptr_new();

	/* Open the .sortdata file. */
	reader->fp = fopen(reader->filename, "rb");
	if (reader->fp == NULL) {
		say_error("%s: file open failed", reader->filename);
		goto fail_free;
	}

	/* Set the read buffer, the PK sort data read is very slow otherwise. */
	size_t buffer_capacity = 8 * 1024 * 1024; /* 8MB */
	reader->buffer = xmalloc(buffer_capacity);
	if (setvbuf(reader->fp, reader->buffer, _IOFBF, buffer_capacity) != 0) {
		say_error("%s: file buffer set failed", reader->filename);
		goto fail_close;
	}

	/* Verify the file magic. */
	if (memtx_sort_data_header_expect(reader, "SORTDATA\n") == NULL)
		goto fail_close;

	/* Verify the file version. */
	if (memtx_sort_data_header_expect(reader, "1\n") == NULL)
		goto fail_close;

	/* Skip the Tarantool version. */
	if (memtx_sort_data_header_expect(reader, "Version: ") == NULL)
		goto fail_close;

	/* Verify the instance UUID. */
	char *uuid_str = memtx_sort_data_header_expect(reader, "Instance: ");
	if (uuid_str == NULL)
		goto fail_close;
	if (strlen(uuid_str) - 1 /* Newline. */ != UUID_STR_LEN) {
		say_error("%s: invalid UUID length: %s",
			  reader->filename, uuid_str);
		goto fail_close;
	}
	uuid_str[UUID_STR_LEN] = '\0';
	struct tt_uuid sortdata_uuid;
	if (tt_uuid_from_string(uuid_str, &sortdata_uuid) != 0) {
		say_error("%s: invalid UUID: %s", reader->filename, uuid_str);
		goto fail_close;
	}
	if (!tt_uuid_is_equal(&sortdata_uuid, instance_uuid)) {
		say_error("%s: unmatched UUID: %s", reader->filename, uuid_str);
		goto fail_close;
	}

	/* Verify the vclock signature. */
	char *vclock_str = memtx_sort_data_header_expect(reader, "VClock: ");
	if (vclock_str == NULL)
		goto fail_close;
	vclock_str[strlen(vclock_str) - 1] = '\0'; /* Drop the newline. */
	struct vclock sortdata_vclock = {};
	if (vclock_from_string(&sortdata_vclock, vclock_str) != 0) {
		say_error("%s: invalid VClock: %s",
			  reader->filename, vclock_str);
		goto fail_close;
	}
	long long sortdata_signature = vclock_sum(&sortdata_vclock);
	long long snapshot_signature = vclock_sum(vclock);
	if (sortdata_signature != snapshot_signature) {
		say_error("%s: unmatched VClock: %s (%lld != %lld)",
			  reader->filename, vclock_str,
			  sortdata_signature, snapshot_signature);
		goto fail_close;
	}

	/* Skip the newline. */
	if (memtx_sort_data_header_expect(reader, "\n") == NULL)
		goto fail_close;

	/* Get the sort data file cardinality. */
	long long cardinality;
	if (fscanf(reader->fp, CARDINALITY_FMT, &cardinality) != 1) {
		say_error("%s: invalid cardinality", reader->filename);
		goto fail_close;
	}
	mh_ptrptr_reserve(reader->old2new, cardinality, NULL);

	/* Get the sort data entry count. */
	unsigned entry_count;
	if (fscanf(reader->fp, ENTRIES_FMT, &entry_count) != 1) {
		say_error("%s: invalid entry count", reader->filename);
		goto fail_close;
	}

	/* Get the sort data entries. */
	for (unsigned i = 0; i < entry_count; i++) {
		struct memtx_sort_data_reader_entry entry;
		long entry_data_length;
		if (fscanf(reader->fp, ENTRY_FMT, &entry.key.space_id,
			   &entry.key.index_id, &entry.offset,
			   &entry.psize, &entry_data_length) != 5) {
			say_error("%s: entry read failed", reader->filename);
			goto fail_close;
		}
		/*
		 * Simple early verification, will fail on tuple
		 * translation later in case the file is truncated.
		 */
		if (entry.psize % entry_data_length != 0) {
			say_error("%s: invalid entry length", reader->filename);
			goto fail_close;
		}
		mh_memtx_sort_data_entries_put(reader->entries,
					       &entry, NULL, NULL);
	}

	say_info("using the memtx sort data from `%s'", reader->filename);
	return reader;

fail_close:
	fclose(reader->fp);
	say_warn("memtx sort data file `%s' ignored", reader->filename);
fail_free:
	mh_memtx_sort_data_entries_delete(reader->entries);
	mh_ptrptr_delete(reader->old2new);
	free(reader->buffer);
	free(reader);
	return NULL;
}

/**
 * Find the space in the sort data file and seek to the PK sort data if exists.
 */
static int
memtx_sort_data_reader_pk_init(struct memtx_sort_data_reader *reader,
			       uint32_t space_id)
{
	/* Check if have sort data for the space in the file. */
	struct memtx_sort_data_key key = {space_id, 0};
	mh_int_t i = mh_memtx_sort_data_entries_find(reader->entries,
						     key, NULL);
	if (i == mh_end(reader->entries)) {
		reader->curr_entry = NULL;
		return 0;
	}

	/* Seek to the PK data and save the entry info to access later. */
	struct memtx_sort_data_reader_entry *entry =
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
				    uint32_t space_id, struct tuple *tuple)
{
	/* First tuple inserted into the space? */
	if (reader->curr_space_id != space_id) {
		if (memtx_sort_data_reader_pk_init(reader, space_id) != 0)
			return -1;
		reader->curr_space_id = space_id;
	}

	/* No sort data for the space? */
	if (reader->curr_entry == NULL)
		return 0;

	/* Consistency check. */
	assert(reader->curr_entry->key.space_id == space_id);

	/*
	 * Associate the old tuple pointer (read from the sort data file) with
	 * the new one (created on insertion). The fread is buffered with user
	 * specified buffer of a large size, see the reader constructor.
	 */
	struct mh_ptrptr_node_t node;
	if (fread(&node.key, sizeof(node.key), 1, reader->fp) != 1) {
		DIAG_SET(reader, "space %u: PK read failed", space_id);
		return -1;
	}
	node.val = tuple;
	mh_ptrptr_put(reader->old2new, &node, NULL, NULL);
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

	struct memtx_sort_data_reader_entry *entry =
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
	struct memtx_sort_data_reader_entry *entry = reader->curr_entry;
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

/* {{{ Garbage collection *****************************************************/

void
memtx_sort_data_collect(const char *snap_filename)
{
	xlog_remove_file(memtx_sort_data_filename(snap_filename), 0, NULL);
}

/* }}} */
