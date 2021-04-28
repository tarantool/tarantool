#ifndef TARANTOOL_BOX_MEMTX_TEXT_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_TEXT_H_INCLUDED


#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;
struct index_def;
struct memtx_engine;

struct index *
memtx_text_index_new(struct memtx_engine *memtx, struct index_def *def);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_MEMTX_TEXT_H_INCLUDED */
