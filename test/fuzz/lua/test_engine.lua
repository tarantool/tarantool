--[[
The test for Tarantool allows you to randomly generate DDL and DML
operations for spaces uses vinyl and memtx engines and toggle
random error injections. All random operations and settings depend
on the seed, which is generated at the very beginning of the test.

by default the script uses a directory `test_engine_dir` in
a current directory. Custom test directory can be specified with
option `--test_dir`. The script will clean up the directory before
testing if it exists.

Usage: tarantool test_engine.lua
]]

local console = require('console')
local fiber = require('fiber')
local fio = require('fio')
local fun = require('fun')
local json = require('json')
local log = require('log')
local math = require('math')

-- Tarantool datatypes.
local datetime = require('datetime')
local decimal = require('decimal')
local uuid = require('uuid')

local test_dir_name = 'test_engine_dir'
local DEFAULT_TEST_DIR = fio.pathjoin(fio.cwd(), test_dir_name)

local params = require('internal.argparse').parse(arg, {
    { 'engine', 'string' },
    { 'h', 'boolean' },
    { 'seed', 'number' },
    { 'test_duration', 'number' },
    { 'test_dir', 'string' },
    { 'verbose', 'boolean' },
    { 'workers', 'number' },
})

local function counter()
    local i = 0
    return function()
        i = i + 1
        return i
    end
end

local index_id_func = counter()

if params.help or params.h then
    print(([[

 Usage: tarantool test_engine.lua [options]

 Options can be used with '--', followed by the value if it's not
 a boolean option. The options list with default values:

   workers <number, 50>                  - number of fibers to run in parallel
   test_duration <number, 2*60>          - test duration time (sec)
   test_dir <string, ./%s>  - path to a test directory
   engine <string, 'vinyl'>              - engine ('vinyl', 'memtx')
   seed <number>                         - set a PRNG seed
   verbose <boolean, false>              - enable verbose logging
   help (same as -h)                     - print this message
]]):format(test_dir_name))
    os.exit(0)
end

-- Number of workers.
local arg_num_workers = params.workers or 50

-- Test duration time.
local arg_test_duration = params.test_duration or 2*60

-- Test directory.
local arg_test_dir = params.test_dir or DEFAULT_TEST_DIR

-- Tarantool engine.
local arg_engine = params.engine or 'vinyl'

local arg_verbose = params.verbose or false

local seed = params.seed or os.time()
math.randomseed(seed)
log.info(string.format('Random seed: %d', seed))

-- The table contains a whitelist of errors that will be ignored
-- by test. Each item is a Lua pattern, special characters
-- should be escaped: ^ $ ( ) % . [ ] * + - ?
-- These characters can also be used in the pattern as normal
-- characters by prefixing them with a "%" character, so "%%"
-- becomes "%", "%[" becomes "[", etc.
local err_pat_whitelist = {
    -- Multi-engine transactions aren't supported, see
    -- https://github.com/tarantool/tarantool/issues/1958 and
    -- https://github.com/tarantool/tarantool/issues/1803.
    "Can not perform index build in a multi-statement transaction",
    -- DDL on a space is locked until the end of the current DDL
    -- operation.
    "the space is already being modified",
    -- The test actively uses transactions that concurrently
    -- changes a data in a space, this can lead to errors below.
    "Transaction has been aborted by conflict",
    "Vinyl does not support rebuilding the primary index of a non%-empty space",
    "fiber is cancelled",
    "fiber slice is exceeded",
    "Can not perform index build in a multi%-statement transaction",
    "Index '[%w_]+' %(HASH%) of space '[%w_]+' %(memtx%) does not support pagination",
    "Can't create or modify index '[%w_]+' in space '[%w_]+': primary key must be unique",
    "Can't create or modify index '[%w_]+' in space '[%w_]+': hint is only reasonable with memtx tree index",
    "Get%(%) doesn't support partial keys and non%-unique indexes",
    "Index '[%w_]+' %(RTREE%) of space '[%w_]+' %(memtx%) does not support max%(%)",
    "Index '[%w_]+' %(RTREE%) of space '[%w_]+' %(memtx%) does not support min%(%)",
    "Failed to allocate %d+ bytes in [%w_]+ for [%w_]+",
    "Storage engine 'memtx' does not support cross%-engine transactions",
    "Storage engine 'vinyl' does not support cross%-engine transactions",
}

local function keys(t)
    assert(next(t) ~= nil)
    local table_keys = {}
    for k, _ in pairs(t) do
        table.insert(table_keys, k)
    end
    return table_keys
end

local function rmtree(path)
    log.info(('CLEANUP %s'):format(path))
    if (fio.path.is_file(path) or fio.path.is_link(path)) then
        fio.unlink(path)
        return
    end
    if fio.path.is_dir(path) then
        for _, p in pairs(fio.listdir(path)) do
            rmtree(fio.pathjoin(path, p))
        end
    end
end

local function rand_char()
    return string.char(math.random(97, 97 + 25))
end

local function rand_string(length)
    length = length or 10
    local res = ''
    for _ = 1, length do
        res = res .. rand_char()
    end
    return res
end

local function oneof(tbl)
    assert(type(tbl) == 'table')
    assert(next(tbl) ~= nil)

    local n = table.getn(tbl)
    local idx = math.random(1, n)
    return tbl[idx]
end

local function unique_ids(max_num_ids)
    local ids = {}
    for i = 1, max_num_ids do
        table.insert(ids, i)
    end
    return function()
        local id = math.random(#ids)
        local v = ids[id]
        assert(v)
        table.remove(ids, id)
        return v
    end
end

-- Forward declaration.
local index_create_op

local function random_int()
    return math.floor(math.random() * 10^12)
end

-- Maximal possible R-tree dimension,
-- see <src/lib/salad/rtree.h>.
local RTREE_MAX_DIMENSION = 20

local RTREE_DIMENSION = math.random(RTREE_MAX_DIMENSION)

-- RTREE is a single index that support arrays, length of arrays
-- depends on a RTREE's dimension.
local function random_array()
    local n = RTREE_DIMENSION * 2
    local arr = {}
    for i = 1, n do
        table.insert(arr, i)
    end
    return arr
end

local function random_map()
    local n = math.random(1, 10)
    local t = {}
    for i = 1, n do
        t[tostring(i)] = i
    end
    return t
end

-- '+' - Numeric.
-- '-' - Numeric.
-- '&' - Numeric.
-- '|' - Numeric.
-- '^' - Numeric.
-- '#' - For deletion.
-- '=' - For assignment.
-- ':' - For string splice.
-- '!' - For insertion of a new field.
-- https://www.tarantool.io/en/doc/latest/concepts/data_model/indexes/#indexes-tree
-- TODO: support varbinary.
-- NOTE: scalar type may include nil, boolean, integer, unsigned,
-- number, decimal, string, varbinary, or uuid values. All these
-- datatypes tested separately, except varbinary, so scalar is
-- unused.
-- NOTE: map is cannot be indexed, so it is unused.
local tarantool_type = {
    ['array'] = {
        generator = random_array,
        operations = {'=', '!'},
    },
    ['boolean'] = {
        generator = function()
            return oneof({true, false})
        end,
        operations = {'=', '!'},
    },
    ['decimal'] = {
        generator = function()
            return decimal.new(random_int())
        end,
        operations = {'+', '-'},
    },
    ['datetime'] = {
        generator = function()
            return datetime.new({timestamp = os.time()})
        end,
        operations = {'=', '!'},
    },
    ['double'] = {
        generator = function()
            return math.random() * 10^12
        end,
        operations = {'-'},
    },
    ['integer'] = {
        generator = random_int,
        operations = {'+', '-'},
    },
    ['map'] = {
        generator = random_map,
        operations = {'=', '!'},
    },
    ['number'] = {
        generator = random_int,
        operations = {'+', '-'},
    },
    ['string'] = {
        generator = rand_string,
        operations = {'=', '!'}, -- XXX: ':'
    },
    ['unsigned'] = {
        generator = function()
            return math.abs(random_int())
        end,
        operations = {'#', '+', '-', '&', '|', '^'},
    },
    ['uuid'] = {
        generator = uuid.new,
        operations = {'=', '!'},
    },
}

-- The name value may be any string, provided that two fields
-- do not have the same name.
-- The type value may be any of allowed types:
-- any | unsigned | string | integer | number | varbinary |
-- boolean | double | decimal | uuid | array | map | scalar,
-- but for creating an index use only indexed fields;
-- (Optional) The is_nullable boolean value specifies whether
-- nil can be used as a field value. See also: key_part.is_nullable.
-- (Optional) The collation string value specifies the collation
-- used to compare field values. See also: key_part.collation.
-- (Optional) The constraint table specifies the constraints that
-- the field value must satisfy.
-- (Optional) The foreign_key table specifies the foreign keys
-- for the field.
--
-- See https://www.tarantool.io/ru/doc/latest/reference/reference_lua/box_space/format/.
local function random_space_format()
    local space_format = {}
    local min_num_fields = table.getn(keys(tarantool_type))
    local max_num_fields = min_num_fields + 10
    local num_fields = math.random(min_num_fields, max_num_fields)
    for i, datatype in ipairs(keys(tarantool_type)) do
        table.insert(space_format, {
            name =('field_%d'):format(i),
            type = datatype,
        })
    end
    for i = min_num_fields - 1, num_fields - min_num_fields - 1 do
        table.insert(space_format, {
            name =('field_%d'):format(i),
            type = oneof(keys(tarantool_type)),
        })
    end

    return space_format
end

-- Iterator types for indexes.
-- See https://www.tarantool.io/en/doc/latest/reference/reference_lua/box_index/pairs/#box-index-iterator-types
-- TODO: support `is_nullable`.
-- TODO: support `multikey`.
-- TODO: support `exclude_null`.
-- TODO: support `pagination`.
local tarantool_indices = {
    HASH = {
        iterator_type = {
            'ALL',
            'EQ',
        },
        data_type = {
            ['boolean'] = true,
            ['decimal'] = true,
            ['double'] = true,
            ['integer'] = true,
            ['number'] = true,
            ['scalar'] = true,
            ['string'] = true,
            ['unsigned'] = true,
            ['uuid'] = true,
            ['varbinary'] = true,
        },
        is_multipart = true,
        is_min_support = false,
        is_max_support = false,
        is_unique_support = true,
        is_non_unique_support = false,
        is_primary_key_support = true,
        is_partial_search_support = false,
    },
    BITSET = {
        iterator_type = {
            'ALL',
            'BITS_ALL_NOT_SET',
            'BITS_ALL_SET',
            'BITS_ANY_SET',
            'EQ',
        },
        data_type = {
            ['string'] = true,
            ['unsigned'] = true,
            ['varbinary'] = true,
        },
        is_multipart = false,
        is_min_support = false,
        is_max_support = false,
        is_unique_support = false,
        is_non_unique_support = true,
        is_primary_key_support = false,
        is_partial_search_support = false,
    },
    TREE = {
        iterator_type = {
            'ALL',
            'EQ',
            'GE',
            'GT',
            'LE',
            'LT',
            'REQ',
        },
        data_type = {
            ['boolean'] = true,
            ['datetime'] = true,
            ['decimal'] = true,
            ['double'] = true,
            ['integer'] = true,
            ['number'] = true,
            ['scalar'] = true,
            ['string'] = true,
            ['unsigned'] = true,
            ['uuid'] = true,
            ['varbinary'] = true,
        },
        is_multipart = true,
        is_min_support = true,
        is_max_support = true,
        is_unique_support = true,
        is_non_unique_support = true,
        is_primary_key_support = true,
        is_partial_search_support = true,
    },
    RTREE = {
        iterator_type = {
            'ALL',
            'EQ',
            'GE',
            'GT',
            'LE',
            'LT',
            'NEIGHBOR',
            'OVERLAPS',
        },
        data_type = {
            ['array'] = true,
        },
        is_multipart = false,
        is_min_support = true,
        is_max_support = true,
        is_unique_support = false,
        is_non_unique_support = true,
        is_primary_key_support = false,
        is_partial_search_support = true,
    },
}

local function select_op(space, idx_type, key)
    local select_opts = {
        iterator = oneof(tarantool_indices[idx_type].iterator_type),
        -- The maximum number of tuples.
        limit = math.random(100, 500),
        -- The number of tuples to skip.
        offset = math.random(100),
        -- A tuple or the position of a tuple (tuple_pos) after
        -- which select starts the search.
        after = box.NULL,
        -- If true, the select method returns the position of
        -- the last selected tuple as the second value.
        fetch_pos = oneof({true, false}),
    }
    space:select(key, select_opts)
end

local function get_op(space, key)
    space:get(key)
end

local function put_op(space, tuple)
    space:put(tuple)
end

local function delete_op(space, tuple)
    space:delete(tuple)
end

local function insert_op(space, tuple)
    space:insert(tuple)
end

local function upsert_op(space, tuple, tuple_ops)
    assert(next(tuple_ops) ~= nil)
    space:upsert(tuple, tuple_ops)
end

local function update_op(space, key, tuple_ops)
    assert(next(tuple_ops) ~= nil)
    space:update(key, tuple_ops)
end

local function replace_op(space, tuple)
    space:replace(tuple)
end

local function bsize_op(space)
    space:bsize()
end

local function len_op(space)
    space:len()
end

local function format_op(space, space_format)
    space:format(space_format)
end

local function setup(engine_name, space_id_func, test_dir, verbose)
    log.info('SETUP')
    assert(engine_name == 'memtx' or
           engine_name == 'vinyl')
    -- Configuration reference (box.cfg),
    -- https://www.tarantool.io/en/doc/latest/reference/configuration/
    local box_cfg_options = {
        checkpoint_count = math.random(5),
        checkpoint_interval = math.random(60),
        checkpoint_wal_threshold = math.random(1024),
        iproto_threads = math.random(1, 10),
        memtx_allocator = oneof({'system', 'small'}),
        memtx_memory = 1024 * 1024,
        memtx_sort_threads = math.random(1, 256),
        memtx_use_mvcc_engine = oneof({true, false}),
        readahead = 16320,
        slab_alloc_factor = math.random(1, 2),
        vinyl_bloom_fpr = math.random(50) / 100,
        vinyl_cache = oneof({0, 2}) * 1024 * 1024,
        vinyl_max_tuple_size = math.random(0, 100000),
        vinyl_memory = 800 * 1024 * 1024,
        vinyl_page_size = math.random(1024, 2048),
        vinyl_range_size = 128 * 1024,
        vinyl_read_threads = math.random(2, 10),
        vinyl_run_count_per_level = math.random(1, 10),
        vinyl_run_size_ratio = math.random(2, 5),
        vinyl_timeout = math.random(1, 5),
        vinyl_write_threads = math.random(2, 10),
        wal_cleanup_delay = 14400,
        wal_dir_rescan_delay = math.random(1, 20),
        wal_max_size = math.random(1024 * 1024 * 1024),
        wal_mode = oneof({'write', 'fsync'}),
        wal_queue_max_size = 16777216,
        work_dir = test_dir,
        worker_pool_threads = math.random(1, 10),
    }
    if verbose then
        box_cfg_options.log_level = 'verbose'
    end
    box.cfg(box_cfg_options)
    log.info('FINISH BOX.CFG')

    log.info('CREATE A SPACE')
    local space_format = random_space_format()
    -- TODO: support `constraint`.
    -- TODO: support `foreign_key`.
    local space_opts = {
        engine = engine_name,
        field_count = oneof({0, table.getn(space_format)}),
        format = space_format,
        if_not_exists = oneof({true, false}),
        is_local = oneof({true, false}),
    }
    if space_opts.engine ~= 'vinyl' then
        space_opts.temporary = oneof({true, false})
    end
    local space_name = ('test_%d'):format(space_id_func())
    local space = box.schema.space.create(space_name, space_opts)
    index_create_op(space)
    index_create_op(space)
    log.info('FINISH SETUP')
    return space
end

local function cleanup_dir(dir)
    log.info('CLEANUP')
    if dir ~= nil then
        rmtree(dir)
        dir = nil -- luacheck: ignore
    end
end

local function teardown(space)
    log.info('TEARDOWN')
    space:drop()
end

-- Indexes,
-- https://www.tarantool.io/en/doc/latest/concepts/data_model/indexes/
-- space_object:create_index(),
-- https://www.tarantool.io/en/doc/latest/reference/reference_lua/box_space/create_index/
local function index_opts(space, is_primary)
    assert(space ~= nil)
    local opts = {
        if_not_exists = false,
        -- TODO: support `sequence`,
        -- TODO: support functional indices.
    }

    if space.engine == 'vinyl' then
        opts.bloom_fpr = math.random(50) / 100
        opts.page_size = math.random(10) * 1024
        opts.range_size = 1073741824
    end

    local indices = fun.iter(keys(tarantool_indices)):filter(
        function(x)
            if tarantool_indices[x].is_primary_key_support == is_primary then
                return x
            end
        end):totable()

    if space.engine == 'vinyl' then
        indices = {'TREE'}
    end

    opts.type = oneof(indices)
    -- Primary key must be unique.
    opts.unique = is_primary and
                  true or
                  tarantool_indices[opts.type].is_unique_support

    -- 'hint' is only reasonable with memtx tree index.
    if space.engine == 'memtx' and
       opts.type == 'TREE' then
        opts.hint = true
    end

    if opts.type == 'RTREE' then
        opts.distance = oneof({'euclid', 'manhattan'})
        opts.dimension = RTREE_DIMENSION
    end

    opts.parts = {}
    local space_format = space:format()
    local idx = opts.type
    local possible_fields = fun.iter(space_format):filter(
        function(x)
            if tarantool_indices[idx].data_type[x.type] == true then
                return x
            end
        end):totable()
    local n_parts = math.random(1, table.getn(possible_fields))
    local id = unique_ids(n_parts)
    for i = 1, n_parts do
        local field_id = id()
        local field = possible_fields[field_id]
        table.insert(opts.parts, { field.name })
        if not tarantool_indices[opts.type].is_multipart and
           i == 1 then
            break
        end
    end

    return opts
end

function index_create_op(space)
    local idx_id = index_id_func()
    local idx_name = 'idx_' .. idx_id
    local is_primary = idx_id == 1
    local opts = index_opts(space, is_primary)
    space:create_index(idx_name, opts)
end

local function index_drop_op(space)
    if not space.enabled then return end
    local idx = oneof(space.index)
    if idx ~= nil then idx:drop() end
end

local function index_alter_op(_, idx, opts)
    assert(idx)
    assert(opts)
    opts.if_not_exists = nil
    idx:alter(opts)
end

local function index_compact_op(_, idx)
    assert(idx)
    idx:compact()
end

local function index_max_op(_, idx)
    assert(idx)
    if not tarantool_indices[idx.type].is_max_support then
        return
    end
    idx:max()
end

local function index_min_op(_, idx)
    assert(idx)
    if not tarantool_indices[idx.type].is_min_support then
        return
    end
    idx:min()
end

local function index_random_op(_, idx)
    assert(idx)
    if idx.type ~= 'TREE' and
       idx.type ~= 'BITSET' and
       idx.type ~= 'RTREE' then
        idx:random()
    end
end

local function index_rename_op(_, idx, idx_name)
    assert(idx)
    idx:rename(idx_name)
end

local function index_stat_op(_, idx)
    assert(idx)
    idx:stat()
end

local function index_get_op(_space, idx, key)
    assert(idx)
    assert(key)
    local index_opts = tarantool_indices[idx.type]
    if not index_opts.is_partial_search_support or
       not index_opts.is_non_unique_support then
        return
    end
    idx:get(key)
end

local function index_select_op(_space, idx, key)
    assert(idx)
    assert(key)
    idx:select(key)
end

local function index_count_op(_, idx)
    assert(idx)
    idx:count()
end

local function index_update_op(_space, key, idx, tuple_ops)
    assert(idx)
    assert(key)
    assert(tuple_ops)
    assert(next(tuple_ops) ~= nil)
    local index_opts = tarantool_indices[idx.type]
    if not index_opts.is_partial_search_support or
       not index_opts.is_non_unique_support then
        return
    end
    idx:update(key, tuple_ops)
end

local function index_delete_op(_space, idx, key)
    assert(idx)
    assert(key)
    local index_opts = tarantool_indices[idx.type]
    if not index_opts.is_partial_search_support or
       not index_opts.is_non_unique_support then
        return
    end
    idx:delete(key)
end

local function random_field_value(field_type)
    local type_gen = tarantool_type[field_type].generator
    assert(type(type_gen) == 'function', field_type)
    return type_gen()
end

-- TODO: support `is_nullable`.
local function random_tuple(space_format)
    local tuple = {}
    for _, field in ipairs(space_format) do
        table.insert(tuple, random_field_value(field.type))
    end

    return tuple
end

-- Example of tuple operations: {{'=', 3, 'a'}, {'=', 4, 'b'}}.
--  - operator (string) – operation type represented in string.
--  - field_identifier (number) – what field the operation will
--    apply to.
--  - value (lua_value) – what value will be applied.
local function random_tuple_operations(space)
    local space_format = space:format()
    local num_fields = math.random(table.getn(space_format))
    local tuple_ops = {}
    local id = unique_ids(num_fields)
    for _ = 1, math.random(num_fields) do
        local field_id = id()
        local field_type = space_format[field_id].type
        local operator = oneof(tarantool_type[field_type].operations)
        local value = random_field_value(field_type)
        table.insert(tuple_ops, {operator, field_id, value})
    end

    return tuple_ops
end

local function random_key(space, idx)
    assert(idx, ('indices: %s'):format(json.encode(space.index)))
    local parts = idx.parts
    local key = {}
    for _, field in ipairs(parts) do
        local type_gen = tarantool_type[field.type].generator
        assert(type(type_gen) == 'function')
        table.insert(key, type_gen())
    end
    return key
end

local function box_snapshot()
    local in_progress = box.info.gc().checkpoint_is_in_progress
    if not in_progress then
        box.snapshot()
    end
end

local ops = {
    -- DML.
    DELETE_OP = {
        func = delete_op,
        args = function(space) return random_key(space, space.index[0]) end,
    },
    INSERT_OP = {
        func = insert_op,
        args = function(space) return random_tuple(space:format()) end,
    },
    SELECT_OP = {
        func = select_op,
        args = function(space)
            local idx = space.index[0]
            return idx.type, random_key(space, idx)
        end,
    },
    GET_OP = {
        func = get_op,
        args = function(space) return random_key(space, space.index[0]) end,
    },
    PUT_OP = {
        func = put_op,
        args = function(space) return random_tuple(space:format()) end,
    },
    REPLACE_OP = {
        func = replace_op,
        args = function(space) return random_tuple(space:format()) end,
    },
    UPDATE_OP = {
        func = update_op,
        args = function(space)
            local pk = space.index[0]
            return random_key(space, pk), random_tuple_operations(space)
        end,
    },
    UPSERT_OP = {
        func = upsert_op,
        args = function(space)
            return random_tuple(space:format()), random_tuple_operations(space)
        end,
    },
    BSIZE_OP = {
        func = bsize_op,
        args = function(_) return end,
    },
    LEN_OP = {
        func = len_op,
        args = function(_) return end,
    },
    FORMAT_OP = {
        func = format_op,
        args = function(_space) return random_space_format() end,
    },

    -- DDL.
    INDEX_ALTER_OP = {
        func = index_alter_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            local is_primary = idx_n == 0
            return space.index[idx_n], index_opts(space, is_primary)
        end,
    },
    INDEX_COMPACT_OP = {
        func = index_compact_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n]
        end,
    },
    INDEX_CREATE_OP = {
        func = index_create_op,
        args = function(_) return end,
    },
    INDEX_DROP_OP = {
        func = index_drop_op,
        args = function(space)
            local indices = keys(space.index)
            -- Don't touch primary index.
            table.remove(indices, 0)
            local idx_n = oneof(indices)
            return space.index[idx_n]
        end,
    },
    INDEX_GET_OP = {
        func = index_get_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            local idx = space.index[idx_n]
            return idx, random_key(space, idx)
        end,
    },
    INDEX_SELECT_OP = {
        func = index_select_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            local idx = space.index[idx_n]
            return idx, random_key(space, idx)
        end,
    },
    INDEX_MIN_OP = {
        func = index_min_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n]
        end,
    },
    INDEX_MAX_OP = {
        func = index_max_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n]
        end,
    },
    INDEX_RANDOM_OP = {
        func = index_random_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n]
        end,
    },
    INDEX_COUNT_OP = {
        func = index_count_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n]
        end,
    },
    INDEX_UPDATE_OP = {
        func = index_update_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            local idx = space.index[idx_n]
            return random_key(space, idx), idx, random_tuple_operations(space)
        end,
    },
    INDEX_DELETE_OP = {
        func = index_delete_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            local idx = space.index[idx_n]
            return idx, random_key(space, idx)
        end,
    },
    INDEX_RENAME_OP = {
        func = index_rename_op,
        args = function(space)
            local idx_name = rand_string()
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n], idx_name
        end,
    },
    INDEX_STAT_OP = {
        func = index_stat_op,
        args = function(space)
            local idx_n = oneof(keys(space.index))
            return space.index[idx_n]
        end,
    },

    TX_BEGIN = {
        func = function()
            if not box.is_in_txn() then
                box.begin()
            end
        end,
        args = function(_) return end,
    },
    TX_COMMIT = {
        func = function()
            if box.is_in_txn() then
                box.commit()
            end
        end,
        args = function(_) return end,
    },
    TX_ROLLBACK = {
        func = function()
            if box.is_in_txn() then
                box.rollback()
            end
        end,
        args = function(_) return end,
    },

    SNAPSHOT_OP = {
        func = box_snapshot,
        args = function(_) return end,
    },
}

local function apply_op(space, op_name)
    local func = ops[op_name].func
    local args = { ops[op_name].args(space) }
    log.info(('%s %s'):format(op_name, json.encode(args)))
    local pcall_args = {func, space, unpack(args)}
    local ok, err = pcall(unpack(pcall_args))
    if ok ~= true then
        log.info(('ERROR: opname "%s", err "%s", args %s'):
                 format(op_name, err, json.encode(args)))
    end
    return err
end

local shared_gen_state

local function worker_func(id, space, test_gen, test_duration)
    log.info(('Worker #%d has started.'):format(id))
    local start = os.clock()
    local gen, param, state = test_gen:unwrap()
    shared_gen_state = state
    local errors = {}
    while os.clock() - start <= test_duration do
        local operation_name
        state, operation_name = gen(param, shared_gen_state)
        if state == nil then
            break
        end
        shared_gen_state = state
        local err = apply_op(space, operation_name)
        table.insert(errors, err)
    end
    log.info(('Worker #%d has finished.'):format(id))
    return errors
end

-- Disabled all enabled error injections.
local function disable_all_errinj(errinj, space)
    local enabled_errinj = fun.iter(errinj):
                           filter(function(i, x)
                               if x.is_enabled then
                                   return i
                               end
                           end):totable()
    for _, errinj_name in pairs(enabled_errinj) do
        local errinj_val = errinj[errinj_name].disable(space)
        errinj[errinj_name].is_enabled = false
        pcall(box.error.injection.set, errinj_name, errinj_val)
    end
end

local function toggle_random_errinj(errinj, max_enabled, space)
    local enabled_errinj = fun.iter(errinj):
                           filter(function(i, x)
                               if x.is_enabled then
                                   return i
                               end
                           end):totable()
    log.info(('Enabled fault injections: %s'):format(
             json.encode(enabled_errinj)))
    local errinj_val, errinj_name
    if table.getn(enabled_errinj) >= max_enabled then
        errinj_name = oneof(enabled_errinj)
        errinj_val = errinj[errinj_name].disable(space)
        errinj[errinj_name].is_enabled = false
    else
        errinj_name = oneof(keys(errinj))
        errinj_val = errinj[errinj_name].enable(space)
        errinj[errinj_name].is_enabled = true
    end
    log.info(string.format('TOGGLE RANDOM ERROR INJECTION: %s -> %s',
                           errinj_name, tostring(errinj_val)))
    local ok, err = pcall(box.error.injection.set, errinj_name, errinj_val)
    if not ok then
        log.info(('Failed to toggle fault injection: %s'):format(err))
    end
end

local enable_errinj_boolean = function(_space) return true end
local disable_errinj_boolean = function(_space) return false end
local enable_errinj_timeout = function(_space)
    return math.random(1, 3)
end
local disable_errinj_timeout = function(_space) return 0 end

-- Tarantool fault injections described in a table returned by
-- `box.error.injection.info()`. However, some fault injections
-- are not safe to use and could lead to false positive bugs.
-- The table below contains fault injections that are useful
-- in fuzzing testing, see details in [1].
--
-- 1. https://github.com/tarantool/tarantool/issues/10236#issuecomment-2225347088
local errinj_set = {
    -- Set to index id (0, 1, 2, ...) to fail index (re)build on
    -- alter.
    ERRINJ_BUILD_INDEX = {
        enable = function(space)
            return math.random(#(keys(space.index)))
        end,
        disable = function(_space)
            return -1
        end,
    },
    -- Set to true to inject delay during index (re)build on alter.
    ERRINJ_BUILD_INDEX_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail index (re)build on alter.
    ERRINJ_BUILD_INDEX_ON_ROLLBACK_ALLOC = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to timeout in seconds to inject after each tuple
    -- processed on index (re)build on alter.
    ERRINJ_BUILD_INDEX_TIMEOUT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to true to inject delay during space format check on
    -- alter.
    ERRINJ_CHECK_FORMAT_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to inject OOM while allocating an index extend
    -- in memtx.
    ERRINJ_INDEX_ALLOC = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail index iterator creation.
    ERRINJ_INDEX_ITERATOR_NEW = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail insertion into memtx hash index.
    ERRINJ_HASH_INDEX_REPLACE = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay freeing memory after dropped memtx
    -- index.
    ERRINJ_MEMTX_DELAY_GC = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay materialization of memtx snapshot.
    ERRINJ_SNAP_COMMIT_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay write of memtx snapshot.
    ERRINJ_SNAP_WRITE_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to timeout in seconds to inject after each tuple
    -- written to memtx snapshot.
    ERRINJ_SNAP_WRITE_TIMEOUT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to true to fail index select.
    ERRINJ_TESTING = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail allocation of memtx tuple.
    ERRINJ_TUPLE_ALLOC = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to timeout to inject before processing each internal
    -- cbus message.
    ERRINJ_TX_DELAY_PRIO_ENDPOINT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to true to fail reading vinyl page from disk.
    ERRINJ_VYRUN_DATA_READ = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay vinyl compaction.
    ERRINJ_VY_COMPACTION_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay lookup of tuple by full key.
    ERRINJ_VY_POINT_LOOKUP_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay vinyl dump.
    ERRINJ_VY_DUMP_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to disable vinyl garbage collection.
    ERRINJ_VY_GC = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to index id to fail vinyl index dump.
    ERRINJ_VY_INDEX_DUMP = {
        enable = function(space)
            return math.random(#space.index + 1) - 1
        end,
        disable = function()
            return -1
        end,
    },
    -- Set to true to fail materialization of vinyl index file.
    ERRINJ_VY_INDEX_FILE_RENAME = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail materialization of vinyl log file.
    ERRINJ_VY_LOG_FILE_RENAME = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail write to vinyl log file.
    ERRINJ_VY_LOG_FLUSH = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to inject delay before consuming vinyl memory quota.
    ERRINJ_VY_QUOTA_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail reading vinyl page from disk.
    ERRINJ_VY_READ_PAGE = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay reading vinyl page from disk.
    ERRINJ_VY_READ_PAGE_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to timeout to inject while reading vinyl page from disk.
    ERRINJ_VY_READ_PAGE_TIMEOUT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to true to fail read view merge during vinyl
    -- dump/compaction due to OOM.
    ERRINJ_VY_READ_VIEW_MERGE_FAIL = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to disable purging empty/failed run files from
    -- log after vinyl dump/compaction.
    ERRINJ_VY_RUN_DISCARD = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail materialization of vinyl run file.
    ERRINJ_VY_RUN_FILE_RENAME = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail vinyl run file write.
    ERRINJ_VY_RUN_WRITE = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay vinyl run file write.
    ERRINJ_VY_RUN_WRITE_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to timeout to inject after writing each tuple during
    -- vinyl dump/compaction.
    ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to timeout to throttle scheduler for after failed vinyl
    -- dump/compaction.
    ERRINJ_VY_SCHED_TIMEOUT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to timeout to inject before squashing vinyl upsert in
    -- background.
    ERRINJ_VY_SQUASH_TIMEOUT = {
        enable = enable_errinj_timeout,
        disable = disable_errinj_timeout,
    },
    -- Set to number of passes before failing allocation of vinyl tuple.
    ERRINJ_VY_STMT_ALLOC = {
        enable = function()
            return math.random(10)
        end,
        disable = function()
            return -1
        end,
    },
    -- Set to true to fail completion of vinyl dump/compaction.
    ERRINJ_VY_TASK_COMPLETE = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail creation of vinyl dump/compaction task
    -- due to OOM.
    ERRINJ_VY_WRITE_ITERATOR_START_FAIL = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to delay write to WAL.
    ERRINJ_WAL_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to number of failures before successful allocation of
    -- disk space for WAL write.
    ERRINJ_WAL_FALLOCATE = {
        enable = function()
            return math.random(10)
        end,
        disable = function()
            return 0
        end,
    },
    -- Set to true to fail WAL write.
    ERRINJ_WAL_IO = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail creation of new WAL file.
    ERRINJ_WAL_ROTATE = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail WAL sync.
    ERRINJ_WAL_SYNC = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to inject delay after WAL sync.
    ERRINJ_WAL_SYNC_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail write to xlog file.
    ERRINJ_WAL_WRITE = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to true to fail write to xlog file.
    ERRINJ_WAL_WRITE_DISK = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to number of bytes to buffer before failing write to xlog file.
    ERRINJ_WAL_WRITE_PARTIAL = {
        enable = function()
            return math.random(65536)
        end,
        disable = function()
            return -1
        end,
    },
    -- Set to true to fail xlog meta read.
    ERRINJ_XLOG_META = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
    -- Set to number of bytes to read before failing xlog data read.
    ERRINJ_XLOG_READ = {
        enable = function()
            return math.random(65536)
        end,
        disable = function()
            return -1
        end,
    },
    -- Set to true to delay xlog file materialization.
    ERRINJ_XLOG_RENAME_DELAY = {
        enable = enable_errinj_boolean,
        disable = disable_errinj_boolean,
    },
}

local function is_error_expected(err_msg, err_whitelist)
    local is_expected = false
    for _, err_pat in ipairs(err_whitelist) do
        if err_msg:match(err_pat) then
            is_expected = true
            break
        end
    end
    return is_expected
end

local function process_errors(error_messages)
    print('Unexpected errors:')
    local found_unexpected_errors = false
    for err_msg, _ in pairs(error_messages) do
        local is_expected = is_error_expected(err_msg, err_pat_whitelist)
        if not is_expected then
            found_unexpected_errors = true
            print(('\t- %s'):format(err_msg))
        end
    end
    if not found_unexpected_errors then
        print('None')
    end
    return found_unexpected_errors
end

local function run_test(num_workers, test_duration, test_dir,
                        engine_name, verbose_mode)

    local socket_path = fio.pathjoin(fio.abspath(test_dir), 'console.sock')
    console.listen(socket_path)
    log.info(('console listen on %s'):format(socket_path))

    if fio.path.exists(test_dir) then
        cleanup_dir(test_dir)
    else
        fio.mkdir(test_dir)
    end

    local workers = {}
    local space_id_func = counter()
    local space = setup(engine_name, space_id_func, test_dir, verbose_mode)

    local test_gen = fun.cycle(fun.iter(keys(ops)))
    local f
    for id = 1, num_workers do
        f = fiber.new(worker_func, id, space, test_gen, test_duration)
        f:set_joinable(true)
        f:name('WRK #' .. id)
        table.insert(workers, f)
    end

    local errinj_f = fiber.new(function(test_duration)
        log.info('Fault injection fiber has started.')
        local max_errinj_in_parallel = 5
        local start = os.clock()
        while os.clock() - start <= test_duration do
            toggle_random_errinj(errinj_set, max_errinj_in_parallel, space)
            fiber.sleep(2)
        end
        log.info('Fault injection fiber has finished.')
    end, arg_test_duration)
    errinj_f:set_joinable(true)
    errinj_f:name('ERRINJ')
    table.insert(workers, errinj_f)

    local error_messages = {}
    for _, fb in ipairs(workers) do
        local ok, res = fiber.join(fb)
        if not ok then
            log.info('ERROR: ' .. json.encode(res))
        end
        if fiber.status(fb) ~= 'dead' then
            fiber.kill(fb)
        end
        if type(res) == 'table' then
            for _, v in ipairs(res) do
                local msg = tostring(v)
                error_messages[msg] = error_messages[msg] or 1
            end
        end
    end
    disable_all_errinj(errinj_set, space)

    teardown(space)

    local exit_code = process_errors(error_messages) and 1 or 0
    os.exit(exit_code)
end

run_test(arg_num_workers, arg_test_duration, arg_test_dir,
         arg_engine, arg_verbose)
