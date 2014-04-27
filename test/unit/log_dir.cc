#include <sys/types.h>
#include <dirent.h>

extern "C" {
#include "test.h"
} /* extern "C" */
#include "log_io.h"
#include "fio.h"
#include "recovery.h" /* wal_write_setlsn() */
#include "memory.h"
#include "fiber.h"
#include "crc32.h"

#define header() note("*** %s ***", __func__)
#define footer() note("*** %s: done ***", __func__)

tt_uuid node_uuid;

static void
testset_create(struct log_dir *dir, int64_t *files, int files_n, int node_n)
{
	char tpl[] = "/tmp/fileXXXXXX";

	struct fio_batch *batch = fio_batch_alloc(1024);
	assert(log_dir_create(dir) == 0);
	strcpy(dir->open_wflags, "wx");
	dir->filetype = "XLOG\n";
	dir->filename_ext = ".xlog";
	dir->dirname = strdup(mkdtemp(tpl));
	dir->mode = 0660;

	struct mh_cluster_t *cluster = mh_cluster_new();
	assert(cluster != NULL);
	for (int f = 0; f < files_n; f++) {
		int64_t lsnsum = 0;
		for (uint32_t node_id = 0; node_id < node_n; node_id++) {
			int64_t lsn = *(files + f * node_n + node_id);
			if (lsn <= 0)
				continue;

			/* Calculate LSNSUM */
			lsnsum += lsn;

			/* Update cluster hash */
			struct node *node = (struct node *)
					calloc(1, sizeof(*node));
			assert(node != NULL);
			node->id = node_id;
			node->current_lsn = lsn;
			uint32_t k = mh_cluster_put(cluster,
				(const struct node **) &node, NULL, NULL);
			assert(k != mh_end(cluster));
		}

		/* Write XLOG */
		struct log_io *l = log_io_open_for_write(dir, lsnsum, &node_uuid,
							 INPROGRESS);
		int rc = wal_write_setlsn(l, batch, cluster);
		assert(rc == 0);
		(void) rc;
		log_io_close(&l);
		mh_cluster_clean(cluster);
	}

	mh_cluster_delete(cluster);
	free(batch);

	int rc = log_dir_scan(dir);
	assert(rc == 0);
	(void) rc;

#if 0
	diag("dir->map dump:");
	diag("file => len(lsns)");
	struct log_meta *meta = log_dir_map_first(&dir->map);
	while (meta != NULL) {
		diag("%lld => %u", (long long) meta->lsnsum, meta->lsn_count);
		meta = log_dir_map_next(&dir->map, meta);
	}

	diag("dir->lsnmap dump:");
	diag("node_id,lsn => file");
	struct log_meta_lsn *meta_lsn = log_dir_lsnmap_first(&dir->lsnmap);
	while (meta_lsn != NULL) {
		diag("%u,%lld => %lld", meta_lsn->node_id,
		     (long long) meta_lsn->lsn,
		     (long long) meta_lsn->meta->lsnsum);
		meta_lsn = log_dir_lsnmap_next(&dir->lsnmap, meta_lsn);
	}
#endif
}

static void
testset_destroy(struct log_dir *dir)
{
	DIR *dh = opendir(dir->dirname);
	assert(dh != NULL);
	struct dirent *dent;
	char path[PATH_MAX];
	while ((dent = readdir(dh)) != NULL) {
		snprintf(path, sizeof(path), "%s/%s", dir->dirname, dent->d_name);
		unlink(path);
	}
	closedir(dh);
	rmdir(dir->dirname);
	log_dir_destroy(dir);
}


static void
test_next(int64_t *files, int files_n, int node_n, int64_t *queries, int query_n)
{
	struct log_dir dir;
	testset_create(&dir, (int64_t *) files, files_n, node_n);

	struct mh_cluster_t *cluster = mh_cluster_new();
	assert(cluster != NULL);

	for (int q = 0; q < query_n; q++) {
		int64_t *query = (int64_t *) queries + q * (node_n + 1);

		/* Update cluster hash */
		for (uint32_t node_id = 0; node_id < node_n; node_id++) {
			int64_t lsn = *(query + node_id);
			if (lsn <= 0)
				continue;

			struct node *node = (struct node *) calloc(1, sizeof(*node));
			assert(node != NULL);
			node->id = node_id;
			node->current_lsn = lsn;
			uint32_t k = mh_cluster_put(cluster,
				(const struct node **) &node, NULL, NULL);
			assert(k != mh_end(cluster));
		}

		int64_t check = *(query + node_n);
		int64_t value = log_dir_next(&dir, cluster);
		is(value, check, "query #%d", q + 1);
		mh_cluster_clean(cluster);
	}

	mh_cluster_delete(cluster);
	testset_destroy(&dir);
}

static int
test1()
{
	plan(36);
	header();

	enum { NODE_N = 4};
	int64_t files[][NODE_N] = {
		{ 10, 0, 0, 0}, /* =10.xlog */
		{ 12, 2, 0, 0}, /* =14.xlog */
		{ 14, 2, 0, 0}, /* =16.xlog */
		{ 14, 2, 2, 0}, /* =18.xlog */
		{ 14, 4, 2, 3}, /* =23.xlog */
		{ 14, 4, 2, 5}, /* =25.xlog */
	};
	enum { FILE_N = sizeof(files) / (sizeof(files[0])) };

	int64_t queries[][NODE_N + 1] = {
		/* not found (lsns are too old) */
		{  0,  0, 0, 0, /* => */ INT64_MAX},
		{  1,  0, 0, 0, /* => */ INT64_MAX},
		{  5,  0, 0, 0, /* => */ INT64_MAX},

		/* =10.xlog (left bound) */
		{  10, 0, 0, 0, /* => */ 10},
		{  10, 1, 0, 0, /* => */ 10},
		{  10, 2, 0, 0, /* => */ 10},
		{  10, 3, 0, 0, /* => */ 10},
		{  10, 4, 0, 0, /* => */ 10},

		/* =10.xlog (middle) */
		{  11, 0, 0, 0, /* => */ 10},
		{  11, 1, 0, 0, /* => */ 10},
		{  11, 2, 0, 0, /* => */ 10},
		{  11, 3, 0, 0, /* => */ 10},
		{  11, 4, 0, 0, /* => */ 10},
		{  11, 5, 3, 6, /* => */ 10},

		/* =10.xlog (right bound) */
		{  12, 0, 0, 0, /* => */ 10},
		{  12, 1, 0, 0, /* => */ 10},
		{  12, 1, 1, 1, /* => */ 10},
		{  12, 1, 2, 5, /* => */ 10},

		/* =14.xlog */
		{  12, 2, 0, 0, /* => */ 14},
		{  12, 3, 0, 0, /* => */ 14},
		{  12, 4, 0, 0, /* => */ 14},
		{  12, 5, 3, 6, /* => */ 14},

		/* =16.xlog */
		{  14, 2, 0, 0, /* => */ 16},
		{  14, 2, 1, 0, /* => */ 16},
		{  14, 2, 0, 1, /* => */ 16},

		/* =18.xlog */
		{  14, 2, 2, 0, /* => */ 18},
		{  14, 2, 4, 0, /* => */ 18},
		{  14, 2, 4, 3, /* => */ 18},
		{  14, 2, 4, 5, /* => */ 18},
		{  14, 4, 2, 0, /* => */ 18},
		{  14, 5, 2, 0, /* => */ 18},

		/* =23.xlog */
		{  14, 4, 2, 3, /* => */ 23},
		{  14, 5, 2, 3, /* => */ 23},

		/* =25.xlog */
		{  14, 4, 2, 5, /* => */ 25},
		{  14, 5, 2, 6, /* => */ 25},
		{ 100, 9, 9, 9, /* => */ 25},
	};
	enum { QUERY_N = sizeof(queries) / (sizeof(queries[0])) };

	test_next((int64_t *) files, FILE_N, NODE_N, (int64_t *) queries, QUERY_N);

	footer();
	return check_plan();
}

int
main(int argc, char *argv[])
{
	(void) argc;

	say_init(argv[0]);
	say_set_log_level(4);
	memory_init();
	fiber_init();
	crc32_init();
	tt_uuid_create(&node_uuid);

	plan(1);
	test1();

	fiber_free();
	memory_free();
	return check_plan();
}
