#include "memtx_text.h"
#include "memtx_engine.h"
#include "space.h"
#include "schema.h"
#include "tuple.h"
#include <salad/inverted_list.h>

struct memtx_text_data {
    t_inverted_list *inverted_position;
};

struct memtx_text_index {
    struct index base;
    // ...
};