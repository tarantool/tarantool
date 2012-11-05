#ifndef INCLUDES_TARANTOOL_ARCHIVE_H
#define INCLUDES_TARANTOOL_ARCHIVE_H
#include "log_io.h"
#include "tuple.h"
#include "txn.h"

void arc_init(const char *arc_dirname);

int arc_write(u32 space,u64 cookie,struct tuple *tuple);

void arc_do_txn(struct txn *txn);

void *arc_writer_thread(void *args);

void arc_stop();


int arc_start();


#endif
