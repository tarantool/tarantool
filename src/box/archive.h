#ifndef INCLUDES_TARANTOOL_ARCHIVE_H
#define INCLUDES_TARANTOOL_ARCHIVE_H
#include <util.h>

struct txn;
struct tuple;

void arc_init(const char *arc_dirname,const char *arc_filename_format, double fsync_delay);

int arc_start();

void arc_save_real_tm(double tm);

void arc_do_txn(struct txn *txn);

void arc_free();


#endif
