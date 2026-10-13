--[[
The test for Tarantool allows you to generate random interleaved (concurrent)
transactions for the memtx and vinyl engines, and verify the consistency of
their execution w.r.t. serializability.

All random operations and settings depend on the seed, which is set in the
`seed` global test parameter.

The test executes infinitely in a round fashion. Each round a number of
transactions are executed, and the results of their execution are saved. Then,
the successfully committed transactions are re-executed serially, based on the
order in which they were committed, and the results of the serial execution are
compared with the results of the interleaved (concurrent) execution. If a
discrepancy is found, the test fails and the current execution round is dumped,
as described below. Similar oracle-based approaches have been proposed by
Zu-Ming Jiang et al. [1] and Wensheng Dou et al. [2].

The test uses luatest's working directory, defined by the environment variable
`VARDIR` (by default, it is `/tmt/t`). In case a discrepancy is found, the test
generates two files, `repro.lua` and `serial.lua`, to facilitate the debugging
process. The former file contains the transactions that were executed in the
current round. The latter file contains the serial transaction execution
schedule for the current round.

The parameters for the test can be tweaked below.

Usage: tarantool test_mvcc.lua

[1]: https://www.usenix.org/conference/osdi23/presentation/jiang
[2]: https://dl.acm.org/doi/10.1109/ICSE48619.2023.00101
]]

local fio = require('fio')
local json = require('json')
local log = require('log')
local has_luatest = pcall(require, 'luatest')
if not has_luatest then
    print('luatest library is required')
    os.exit(1)
end
local server = require('luatest.server')
local yaml = require('yaml')

-- Global test parameters.

local params = require('internal.argparse').parse(arg, {
    {'help', 'boolean'},
    {'h', 'boolean'},
    {'test_dir', 'string'},
    {'seed', 'number'},
    {'n_rounds', 'number'},
    {'tx_cnt', 'number'},
    {'ro_tx_cnt', 'number'},
    {'stmt_cnt', 'number'},
    {'p_rollback', 'number'},
    {'p_commit', 'number'},
    {'max_key', 'number'},
    {'p_null_key', 'number'},
    {'max_offset', 'number'},
    {'max_limit', 'number'},
    {'p_next_page', 'number'},
    {'prefill_cnt', 'number'},
    {'p_next_tuples', 'number'},
    {'p_gc', 'number'},
    {'p_read_view', 'number'},
})

local DEFAULT_TEST_DIR_NAME = 'test_mvcc'
local DEFAULT_TEST_DIR = fio.pathjoin(fio.cwd(), DEFAULT_TEST_DIR_NAME)
-- Default random seed.
local DEFAULT_SEED = os.time()
-- Default number of rounds to run.
local DEFAULT_N_ROUNDS = 1000
-- Default number of transactions in 1 round.
local DEFAULT_TX_CNT = 32
-- Default number of read-only transactions.
local DEFAULT_RO_TX_CNT = 8
-- Default number of transaction statements in 1 round.
local DEFAULT_STMT_CNT = 320
-- Default probability of rollback.
local DEFAULT_P_ROLLBACK = 0.05
-- Default probability of commit.
local DEFAULT_P_COMMIT = 0.1
-- Default maximum random unsigned key (minimum is 1).
local DEFAULT_MAX_KEY = 8
-- Default probability of NULL value for nullable keys.
local DEFAULT_P_NULL_KEY = 0.1
-- Default maximum select offset (minimum is 0).
local DEFAULT_MAX_OFFSET = 4
-- Default maximum select limit (minimum is 1).
local DEFAULT_MAX_LIMIT = 4
-- Default probability of fetching the next page of a paginated select.
local DEFAULT_P_NEXT_PAGE = 0.5
-- Default number of random tuples put into each space before a round.
local DEFAULT_PREFILL_CNT = 32
-- Default probability of fetching the next tuples of a held iterator.
local DEFAULT_P_NEXT_TUPLES = 0.5
-- Default probability of a story garbage collection step between statements.
local DEFAULT_P_GC = 0.1
-- Default probability of opening a read view between statements.
local DEFAULT_P_READ_VIEW = 0.02

if params.help then
    print(([[

 Usage: tarantool test_mvcc.lua [options]

 Options can be used with '--', followed by the value if it's not
 a boolean option. The options list with default values:

   test_dir <string, ./%s>  - path to a working directory for the test
   seed <number>             - set a PRNG seed
   n_rounds <number, %d>     - number of test execution rounds
   tx_cnt <number, %d>       - number of transactions in 1 round
   ro_tx_cnt <number, %d>    - number of read-only transactions in 1 round
   stmt_cnt <number, %d>     - number of transaction statements in 1 round
   p_rollback <number, %.2f> - probability of transaction rollback
   p_commit <number, %.2f>   - probability of transaction commit
   max_key <number, %d>      - maximum random unsigned key used (minimum is 1)
   p_null_key <number, %.2f> - probability of NULL value for nullable keys
   max_offset <number, %d>   - maximum select offset (minimum is 0)
   max_limit <number, %d>    - maximum select limit (minimum is 1)
   p_next_page <number, %.2f> - probability of fetching the next page of a
                                paginated select
   prefill_cnt <number, %d>  - number of random tuples put into each space
                               before a round
   p_next_tuples <number, %.2f> - probability of fetching the next tuples of
                                  a held iterator
   p_gc <number, %.2f>       - probability of a story garbage collection step
                               between statements
   p_read_view <number, %.2f> - probability of opening a read view between
                                statements
   help (same as -h)         - print this message
]]):format(DEFAULT_TEST_DIR_NAME, DEFAULT_N_ROUNDS, DEFAULT_TX_CNT,
           DEFAULT_RO_TX_CNT, DEFAULT_STMT_CNT, DEFAULT_P_ROLLBACK,
           DEFAULT_P_COMMIT, DEFAULT_MAX_KEY, DEFAULT_P_NULL_KEY,
           DEFAULT_MAX_OFFSET, DEFAULT_MAX_LIMIT, DEFAULT_P_NEXT_PAGE,
           DEFAULT_PREFILL_CNT, DEFAULT_P_NEXT_TUPLES, DEFAULT_P_GC,
           DEFAULT_P_READ_VIEW))
    os.exit(0)
end

-- Path to the working directory of the test.
local ARG_TEST_DIR = params.test_dir or DEFAULT_TEST_DIR
-- Random seed.
local ARG_SEED = params.seed or DEFAULT_SEED
-- Number of rounds to run.
local ARG_N_ROUNDS = params.n_rounds or DEFAULT_N_ROUNDS
-- Number of transactions in 1 round.
local ARG_TX_CNT = params.tx_cnt or DEFAULT_TX_CNT
-- Number of read-only transactions.
local ARG_RO_TX_CNT = params.ro_tx_cnt or DEFAULT_RO_TX_CNT
-- Number of transaction statements in 1 round.
local ARG_STMT_CNT = params.stmt_cnt or DEFAULT_STMT_CNT
-- Probability of rollback.
local ARG_P_ROLLBACK = params.p_rollback or DEFAULT_P_ROLLBACK
-- Probability of commit.
local ARG_P_COMMIT = params.p_commit or DEFAULT_P_COMMIT
-- Maximum random unsigned key (minimum is 1).
local ARG_MAX_KEY = params.max_key or DEFAULT_MAX_KEY
-- Probability of NULL value for nullable keys.
local ARG_P_NULL_KEY = params.p_null_key or DEFAULT_P_NULL_KEY
-- Maximum select offset (minimum is 0).
local ARG_MAX_OFFSET = params.max_offset or DEFAULT_MAX_OFFSET
-- Maximum select limit (minimum is 1).
local ARG_MAX_LIMIT = params.max_limit or DEFAULT_MAX_LIMIT
-- Probability of fetching the next page of a paginated select.
local ARG_P_NEXT_PAGE = params.p_next_page or DEFAULT_P_NEXT_PAGE
-- Number of random tuples put into each space before a round.
local ARG_PREFILL_CNT = params.prefill_cnt or DEFAULT_PREFILL_CNT
-- Probability of fetching the next tuples of a held iterator.
local ARG_P_NEXT_TUPLES = params.p_next_tuples or DEFAULT_P_NEXT_TUPLES
-- Probability of a story garbage collection step between statements.
local ARG_P_GC = params.p_gc or DEFAULT_P_GC
-- Probability of opening a read view between statements.
local ARG_P_READ_VIEW = params.p_read_view or DEFAULT_P_READ_VIEW

-- Global test constants.

-- Transaction operation types.
-- Data manipulation language.
local DML = 0
-- Data query language.
local DQL = 1
-- Transaction control language (BEGIN, COMMIT, ROLLBACK).
local TCL = 2
-- Statements executed outside of the transactions, like the story garbage
-- collection.
local ENV = 3

-- DML operation subtypes.
local DELETE = 0
local INSERT = 1
local REPLACE = 2
local UPDATE = 3
local UPSERT = 4

-- DQL operation subtypes.
local SELECT = 0
local GET = 1
local COUNT = 2
local MIN = 3
local MAX = 4
local OFFSET = 5
local PAGE = 6
local NEXT_PAGE = 7
local ITERATOR = 8
local NEXT_TUPLES = 9

-- Transaction isolation levels, besides the default one. The linearizable
-- level needs synchronous replication.
local ISOLATION_LEVELS = {'best-effort', 'read-committed', 'read-confirmed'}

-- Format of test space.
local SPACE_FORMAT = {
    {'1', type = 'uint'},
    {'2', type = 'uint'},
    {'3', type = 'uint', is_nullable = true},
    {'4', type = 'uint', is_nullable = true},
    {'5', type = 'array'},
}

-- A DQL operation format takes the transaction id (`has_tid`), the space, the
-- index, the key (`key_cnt` or `full_key`), the iterator (`has_iter`) and the
-- offset and the limit (`has_window`) or the limit alone (`has_limit`) in this
-- order.
local generic_idx_meta = {
    ro_ops = {
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 0,
            fmt = 'box.space.%s.index[%d]:select({}, ' ..
                  '{fullscan = true})',
        },
        {
            type = DQL,
            subtype = SELECT,
            full_key = true,
            has_iter = true,
            fmt = 'box.space.%s.index[%d]:select({%s}, ' ..
                  '{iterator = \'%s\', fullscan = true})',
        },
        {
            type = DQL,
            subtype = GET,
            full_key = true,
            fmt = 'box.space.%s.index[%d]:get{%s}',
        },
        {
            type = DQL,
            subtype = COUNT,
            key_cnt = 0,
            fmt = 'box.space.%s.index[%d]:count()',
        },
        {
            type = DQL,
            subtype = COUNT,
            full_key = true,
            has_iter = true,
            fmt = 'box.space.%s.index[%d]:count({%s}, ' ..
                  '{iterator = \'%s\'})',
        },
    },
    dml_ops = {
        {
            type = DML,
            subtype = UPDATE,
            fmt = 'box.space.%s.index[%d]:update({%s}, ' ..
                  '{%s, %s})',
        },
        {
            type = DML,
            subtype = DELETE,
            fmt = 'box.space.%s.index[%d]:delete{%s}',
        },
        {
            type = DML,
            subtype = INSERT,
            fmt = 'box.space.%s:insert{%s}',
        },
        {
            type = DML,
            subtype = REPLACE,
            fmt = 'box.space.%s:replace{%s}',
        },
        {
            type = DML,
            subtype = UPSERT,
            fmt = 'box.space.%s:upsert({%s}, {%s, %s})',
        },
    },
}

local tree_idx_meta = table.deepcopy(generic_idx_meta)
tree_idx_meta.iters = {'EQ', 'REQ', 'GT', 'GE', 'LT', 'LE'}
local tree_idx_meta_additional_ops = {
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 0,
            has_iter = true,
            fmt = 'box.space.%s.index[%d]:select({}, ' ..
                  '{iterator = \'%s\', fullscan = true})',
        },
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 1,
            has_iter = true,
            fmt = 'box.space.%s.index[%d]:select({%s}, ' ..
                  '{iterator = \'%s\', fullscan = true})',
        },
        {
            type = DQL,
            subtype = OFFSET,
            key_cnt = 0,
            has_iter = true,
            has_window = true,
            fmt = 'box.space.%s.index[%d]:select({}, ' ..
                  '{iterator = \'%s\', offset = %d, limit = %d})',
        },
        {
            type = DQL,
            subtype = OFFSET,
            key_cnt = 1,
            has_iter = true,
            has_window = true,
            fmt = 'box.space.%s.index[%d]:select({%s}, ' ..
                  '{iterator = \'%s\', offset = %d, limit = %d})',
        },
        {
            type = DQL,
            subtype = OFFSET,
            full_key = true,
            has_iter = true,
            has_window = true,
            fmt = 'box.space.%s.index[%d]:select({%s}, ' ..
                  '{iterator = \'%s\', offset = %d, limit = %d})',
        },
        {
            type = DQL,
            subtype = COUNT,
            key_cnt = 0,
            has_iter = true,
            fmt = 'box.space.%s.index[%d]:count({}, {iterator = \'%s\'})',
        },
        {
            type = DQL,
            subtype = COUNT,
            key_cnt = 1,
            has_iter = true,
            fmt = 'box.space.%s.index[%d]:count({%s}, ' ..
                  '{iterator = \'%s\'})',
        },
        {
            type = DQL,
            subtype = MIN,
            key_cnt = 0,
            fmt = 'box.space.%s.index[%d]:min()',
        },
        {
            type = DQL,
            subtype = MIN,
            key_cnt = 1,
            fmt = 'box.space.%s.index[%d]:min({%s})',
        },
        {
            type = DQL,
            subtype = MIN,
            full_key = true,
            fmt = 'box.space.%s.index[%d]:min({%s})',
        },
        {
            type = DQL,
            subtype = MAX,
            key_cnt = 0,
            fmt = 'box.space.%s.index[%d]:max()',
        },
        {
            type = DQL,
            subtype = MAX,
            key_cnt = 1,
            fmt = 'box.space.%s.index[%d]:max({%s})',
        },
        {
            type = DQL,
            subtype = MAX,
            full_key = true,
            fmt = 'box.space.%s.index[%d]:max({%s})',
        },
        {
            type = DQL,
            subtype = PAGE,
            has_tid = true,
            key_cnt = 0,
            has_iter = true,
            has_limit = true,
            fmt = 'select_first_page(%d, \'%s\', %d, {}, \'%s\', %d)',
        },
        {
            type = DQL,
            subtype = PAGE,
            has_tid = true,
            key_cnt = 1,
            has_iter = true,
            has_limit = true,
            fmt = 'select_first_page(%d, \'%s\', %d, {%s}, \'%s\', %d)',
        },
        {
            type = DQL,
            subtype = PAGE,
            has_tid = true,
            full_key = true,
            has_iter = true,
            has_limit = true,
            fmt = 'select_first_page(%d, \'%s\', %d, {%s}, \'%s\', %d)',
        },
        {
            type = DQL,
            subtype = ITERATOR,
            has_tid = true,
            key_cnt = 0,
            has_iter = true,
            fmt = 'open_iterator(%d, \'%s\', %d, {}, \'%s\')',
        },
        {
            type = DQL,
            subtype = ITERATOR,
            has_tid = true,
            key_cnt = 1,
            has_iter = true,
            fmt = 'open_iterator(%d, \'%s\', %d, {%s}, \'%s\')',
        },
        {
            type = DQL,
            subtype = ITERATOR,
            has_tid = true,
            full_key = true,
            has_iter = true,
            fmt = 'open_iterator(%d, \'%s\', %d, {%s}, \'%s\')',
        },
    }
for _, op in ipairs(tree_idx_meta_additional_ops) do
    table.insert(tree_idx_meta.ro_ops, op)
end

local hash_idx_meta = table.deepcopy(generic_idx_meta)
hash_idx_meta.iters = {'EQ'}

local function create_idx_meta(fields, type)
    type = type or 'TREE'
    local res
    if type == 'TREE' then
        res = table.copy(tree_idx_meta)
    else
        res = table.copy(hash_idx_meta)
    end
    res.type = type
    res.fields = fields
    return res
end

-- Metadata of tested engines based on SCHEMA_CODE. Each index defined in
-- SCHEMA_CODE requires a metadata entry.
local ENGINES = {
    {
        name = 'memtx',
        space = 'm',
        idxs = {
            create_idx_meta({{is_nullable = false}, {is_nullable = false}}),
            create_idx_meta({{is_nullable = true}, {is_nullable = false}}),
            create_idx_meta({{is_nullable = false}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = true}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = true}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = false}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = false}, {is_nullable = false}},
                            'HASH'),
            create_idx_meta({{is_nullable = false}, {is_nullable = false}}),
            create_idx_meta({{is_nullable = false}}),
            create_idx_meta({{is_nullable = false}}),
            create_idx_meta({{is_nullable = false}}),
            create_idx_meta({{is_nullable = false}}),
        }
    },
    {
        name = 'vinyl',
        space = 'v',
        idxs = {
            create_idx_meta({{is_nullable = false}, {is_nullable = false}}),
            create_idx_meta({{is_nullable = true}, {is_nullable = false}}),
            create_idx_meta({{is_nullable = false}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = true}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = true}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = false}, {is_nullable = true}}),
            create_idx_meta({{is_nullable = false}}),
            create_idx_meta({{is_nullable = false}}),
        }
    },
}

-- Global test variables.

-- File for dumping the executed transactions for the current round.
local repro_file
-- File for dumping the serial transaction execution schedule for the current
-- round.
local serial_file
-- List of all transaction statements for the current round.
local stmts
-- Mask of read-only transactions for the current round.
local ro_txs_mask
-- Mask of transactions that got successfully committed during the current
-- round.
local committed_txs_mask
-- Mask of transactions that had an erroneous DML operation for the current
-- round.
local bad_dml_txs_mask
-- Serial schedule of transactions for the current round.
local serial
-- Code putting the initial tuples into the spaces for the current round.
local prefill_code
-- Number of committed read-write transactions in the current round.
local rw_commit_cnt
-- Read views opened during the current round: the number of read-write
-- transactions committed before each one and, at the end of the round, the
-- contents of the memtx indexes it sees.
local read_views
-- Server used for executing random transactions with MVCC enabled.
local random_tx_executor
-- Server used for checking consistency of executed transactions with MVCC
-- disabled.
local consistency_checker

local function generate_txn_repro_stmt(stmt)
    if stmt.type == ENV then
        if stmt.res == nil then
            return ('%s\n'):format(stmt.str)
        end
        return ('%s -- %s\n'):format(stmt.str, json.encode(stmt.res))
    end
    return ('tx%d(%q) -- %s\n'):format(stmt.tid, stmt.str,
                                      json.encode(stmt.res))
end

local function generate_txn_serial_stmt(stmt)
    return ('%s -- tx%d: %s\n'):format(stmt.str, stmt.tid,
                                       json.encode(stmt.res))
end

local INIT_CODE = [[
local ffi = require('ffi')
local fio = require('fio')
local fiber = require('fiber')
local json = require('json')
local log = require('log')
local txn_proxy = require('test.box.lua.txn_proxy')

local work_dir = fio.pathjoin(fio.cwd(), '%s')
if fio.path.exists(work_dir) then
    fio.rmtree(work_dir)
end
fio.mkdir(work_dir)

box.cfg{memtx_use_mvcc_engine = %s, work_dir = work_dir}
fiber.set_max_slice({warn = 6000, err = 6000})

]]

local function generate_init_code(work_dir, memtx_use_mvcc_engine)
    return (INIT_CODE):format(work_dir, memtx_use_mvcc_engine)
end

-- Schema definition code. Each index defined here requires a metadata entry in
-- the ENGINES tables.
local SCHEMA_CODE = [[
box.schema.func.create('func',
    {body = 'function(tuple) return {tuple[1], tuple[2]} end',
     is_deterministic = true,
     is_sandboxed = true, if_not_exists = true})
box.schema.func.create('multikey_func', {
    body = [=[
        function(tuple)
            local keys = {}
            for _, key in ipairs(tuple[5]) do
                table.insert(keys, {key})
            end
            return keys
        end
    ]=],
    is_deterministic = true,
    is_sandboxed = true,
    if_not_exists = true,
    opts = {is_multikey = true},
})
box.schema.space.create('m', {engine = 'memtx'})
box.space.m:create_index('p', {parts = {
    {1, 'uint'},
    {2, 'uint'}}})
box.space.m:create_index('s1', {parts = {
    {3, 'uint', exclude_null = true},
    {2, 'uint'}}})
box.space.m:create_index('s2', {parts = {
    {1, 'uint'},
    {4, 'uint', exclude_null = true}}})
box.space.m:create_index('s3', {parts = {
    {3, 'uint', exclude_null = true},
    {4, 'uint', exclude_null = true}}})
box.space.m:create_index('s4', {parts = {
    {3, 'uint', is_nullable = true},
    {4, 'uint', is_nullable = true}}})
box.space.m:create_index('s5', {unique = false, parts = {
    {1, 'uint', is_nullable = false},
    {4, 'uint', is_nullable = true}}})
box.space.m:create_index('s6', {type = 'HASH', parts = {
    {1, 'uint'},
    {2, 'uint'}}})
box.space.m:create_index('s7', {parts = {
    {1, 'uint'},
    {2, 'uint'}}, func = 'func'})
box.space.m:create_index('s8', {
    parts = {{field = 5, path = '[*]'}}, unique = true})
box.space.m:create_index('s9', {
    parts = {{1, 'unsigned'}}, func = 'multikey_func', unique = true})
box.space.m:create_index('s10', {
    parts = {{field = 5, path = '[*]'}}, unique = false})
box.space.m:create_index('s11', {
    parts = {{1, 'unsigned'}}, func = 'multikey_func', unique = false})
box.schema.space.create('v', {engine = 'vinyl'})
box.space.v:create_index('p', {parts = {
    {1, 'uint'},
    {2, 'uint'}}})
box.space.v:create_index('s1', {parts = {
    {3, 'uint', exclude_null = true},
    {2, 'uint'}}})
box.space.v:create_index('s2', {parts = {
    {1, 'uint'},
    {4, 'uint', exclude_null = true}}})
box.space.v:create_index('s3', {parts = {
    {3, 'uint', exclude_null = true},
    {4, 'uint', exclude_null = true}}})
box.space.v:create_index('s4', {parts = {
    {3, 'uint', is_nullable = true},
    {4, 'uint', is_nullable = true}}})
box.space.v:create_index('s5', {unique = false, parts = {
    {1, 'uint', is_nullable = false},
    {4, 'uint', is_nullable = true}}})
box.space.v:create_index('s6', {
    parts = {{field = 5, path = '[*]'}}, unique = true})
box.space.v:create_index('s7', {
    parts = {{field = 5, path = '[*]'}}, unique = false})
]]

local function generate_schema_code()
    return SCHEMA_CODE
end

-- Helpers of the statements that keep a state across the statements of a
-- transaction. The state is keyed by the transaction id, so that the serial
-- re-execution of the statements of a transaction finds the same state.
local HELPERS_CODE = [[
-- The last fetched page of each transaction: the select arguments and the
-- position to continue from.
pages = {}

function select_first_page(tid, space, idx, key, iter, limit)
    local tuples, pos = box.space[space].index[idx]:select(key, {
        iterator = iter, limit = limit, fetch_pos = true, fullscan = true})
    pages[tid] = {space = space, idx = idx, key = key, iter = iter, pos = pos}
    return tuples
end

function select_next_page(tid, limit)
    local page = pages[tid]
    if page == nil or page.pos == nil then
        return {}
    end
    local tuples, pos = box.space[page.space].index[page.idx]:select(page.key, {
        iterator = page.iter, limit = limit, after = page.pos,
        fetch_pos = true, fullscan = true})
    page.pos = pos
    return tuples
end

-- The iterator held open by each transaction across its other statements.
iterators = {}

function open_iterator(tid, space, idx, key, iter)
    local gen, param, state = box.space[space].index[idx]:pairs(key, {
        iterator = iter}):unwrap()
    iterators[tid] = {gen = gen, param = param, state = state}
    return true
end

function next_tuples(tid, count)
    local iterator = iterators[tid]
    local tuples = {}
    if iterator == nil or iterator.state == nil then
        return tuples
    end
    for _ = 1, count do
        local tuple
        iterator.state, tuple = iterator.gen(iterator.param, iterator.state)
        if iterator.state == nil then
            break
        end
        table.insert(tuples, tuple)
    end
    return tuples
end

-- The read views opened during a round.
read_views = {}

function open_read_view(i)
    read_views[i] = box.read_view.open()
end

-- The contents of the memtx indexes seen by a read view, which is closed.
function read_view_state(i)
    local rv = read_views[i]
    local state = {}
    for idx_id = 0, #box.space.m.index do
        state[idx_id + 1] = rv.space.m.index[idx_id]:select({}, {
            fullscan = true})
    end
    rv:close()
    return state
end

-- The number of stories of the memtx transaction manager.
function stories_count()
    local stats = box.stat.memtx.tx().mvcc.tuples
    return stats.used.stories.count + stats.tracking.stories.count +
           stats.read_view.stories.count
end

]]

local function generate_txn_proxy(id)
    return ('local tx%d = txn_proxy:new()\n'):format(id)
end

local EXIT_CODE = [[
os.exit()

]]

local function dump_repro()
    repro_file:write(generate_init_code('repro', true) ..
                     generate_schema_code() .. HELPERS_CODE .. prefill_code)
    for i = 1, ARG_TX_CNT do
        repro_file:write(generate_txn_proxy(i))
    end
    repro_file:write('\n')
    for _, stmt in ipairs(stmts) do
        repro_file:write(generate_txn_repro_stmt(stmt))
    end
    repro_file:write(EXIT_CODE)
end

local function dump_serial()
    serial_file:write(generate_init_code('serial', false) ..
                      generate_schema_code() .. HELPERS_CODE .. prefill_code)
    for _, stmt in ipairs(serial) do
        serial_file:write(generate_txn_serial_stmt(stmt))
    end
    serial_file:write(EXIT_CODE)
end

local function tx_call(tx, operation)
    local ok, res = pcall(tx._strm.eval, tx._strm, 'return ' .. operation.str)
    table.insert(stmts, {
        tid     = tx.id,
        eng     = tx.eng.name,
        type    = operation.type,
        str     = operation.str,
        ordered = operation.ordered,
        ok      = ok,
        res     = res,
    })
    if ok and operation.type == DML then
        tx.ro = false
    end
    if ok and operation.subtype == PAGE then
        tx.page = true
    end
    if ok and operation.subtype == ITERATOR then
        tx.iterator = true
    end
    if not ok then
        if res.message == 'Transaction has been aborted by conflict' then
            -- TODO(gh-11397): add probability for keeping this transaction.
            tx:rollback()
            return ok, res
        end
        -- Set `bad_dml` to filter this transaction from RO ones later on.
        if operation.type == DML then
            tx.bad_dml = true
        end
    end
    if tx._strm._conn.state == 'error' then
        dump_repro()
        log.info('connection in error state: ' .. tx._strm._conn.error)
        os.exit(1)
    end
    return ok, res
end

-- Execute a statement outside of the transactions on the executor, recording
-- it and its result for the reproducer.
local function env_call(str)
    local stmt = {type = ENV, str = str}
    table.insert(stmts, stmt)
    local ok, res = pcall(random_tx_executor.eval, random_tx_executor,
                          'return ' .. str)
    if not ok then
        dump_repro()
        log.info(('statement \'%s\' failed: %s'):format(str, tostring(res)))
        os.exit(1)
    end
    stmt.res = res
    return res
end

local function tx_begin(tx)
    assert(not tx.running and not tx.committed and not tx.aborted)
    local str = 'box.begin()'
    if math.random(2) == 1 then
        str = ('box.begin({txn_isolation = \'%s\'})'):format(
            ISOLATION_LEVELS[math.random(#ISOLATION_LEVELS)])
    end
    tx({
        tid  = tx.id,
        type = TCL,
        str  = str,
    })
    tx.running = true
end

local function tx_rollback(tx)
    assert(tx.running and not tx.committed and not tx.aborted)
    local ok, err = tx({
        tid  = tx.id,
        type = TCL,
        str  = 'box.rollback()',
    })
    if not ok then
        log.info('stream:rollback failed: ' .. err.message)
        os.exit(1)
    end
    tx.running = false
    tx.aborted = true
end

local function tx_commit(tx)
    assert(tx.running and not tx.committed and not tx.aborted)
    local ok, err = tx({
        tid  = tx.id,
        type = TCL,
        str  = 'box.commit()',
    })
    if not ok then
        if err.message ~= 'Transaction has been aborted by conflict' then
            log.info('stream:commit failed unexpectedly: ' .. err.message)
            os.exit(1)
        end
        tx.running = false
        tx.aborted = true
        tx.str = 'box.rollback()'
        return false
    end
    tx.running = false
    tx.committed = true
    if not tx.ro then
        rw_commit_cnt = rw_commit_cnt + 1
    end
    return true
end

local function tx_new(conn, id)
    local mt = {
        __index = {
            begin    = tx_begin,
            commit   = tx_commit,
            rollback = tx_rollback,
        },
        __call = tx_call,
    }
    return setmetatable({
        aborted   = false,
        bad_dml   = false,
        committed = false,
        eng       = ENGINES[math.random(#ENGINES)],
        id        = id,
        iterator  = false,
        page      = false,
        ro        = true,
        running   = false,
        _strm     = conn:new_stream(),
    }, mt)
end

local function random_number()
    return math.random(ARG_MAX_KEY)
end

local function gen_array()
    local array = {}
    for _ = 1, math.random(4) do
        table.insert(array, math.random(ARG_MAX_KEY * 8))
    end
    return ('{%s}'):format(table.concat(array, ', '))
end

local function gen_field(field)
    if field.is_nullable and math.random() <= ARG_P_NULL_KEY then
        return 'box.NULL'
    end
    if field.type == 'array' then
        return gen_array()
    end
    return random_number()
end

local function gen_key(fields)
    local key = {}
    for _ in ipairs(fields) do
        table.insert(key, gen_field({is_nullable = false}))
    end
    return key
end

local function gen_tuple()
    local tuple = {}
    for _, field in ipairs(SPACE_FORMAT) do
        table.insert(tuple, gen_field(field))
    end
    return tuple
end

-- Statements replacing random tuples into the spaces. Replacements failing
-- on duplicate secondary keys are ignored.
local function generate_prefill_code()
    local code = {}
    for _ = 1, ARG_PREFILL_CNT do
        for _, eng in ipairs(ENGINES) do
            table.insert(code, ('pcall(box.space.%s.replace, box.space.%s, ' ..
                                '{%s})'):format(eng.space, eng.space,
                                                table.concat(gen_tuple(),
                                                             ', ')))
        end
    end
    return table.concat(code, '\n') .. '\n'
end

local function gen_update(fields)
    return {('{\'=\', 3, %s}'):format(gen_field(fields[1])),
            ('{\'=\', 4, %s}'):format(gen_field(fields[2]))}
end

local function gen_random_operation(tx)
    if tx.page and math.random() < ARG_P_NEXT_PAGE then
        return {
            type = DQL,
            subtype = NEXT_PAGE,
            ordered = true,
            str = ('select_next_page(%d, %d)'):format(
                tx.id, math.random(ARG_MAX_LIMIT)),
        }
    end
    if tx.iterator and math.random() < ARG_P_NEXT_TUPLES then
        return {
            type = DQL,
            subtype = NEXT_TUPLES,
            ordered = true,
            str = ('next_tuples(%d, %d)'):format(
                tx.id, math.random(ARG_MAX_LIMIT)),
        }
    end
    local ro = tx.id <= ARG_RO_TX_CNT
    local eng = tx.eng
    local idx_id = math.random(#eng.idxs) - 1
    local idx = eng.idxs[idx_id + 1]
    local ops = (ro or math.random(2) == 1) and idx.ro_ops or idx.dml_ops
    local op = ops[math.random(#ops)]
    local keys = gen_key(idx.fields)
    local key = table.concat(keys, ', ')
    -- A tree index returns its tuples in the order of the index.
    op.ordered = idx.type == 'TREE'
    if op.type == DQL then
        local args = op.has_tid and {tx.id, eng.space, idx_id} or
                     {eng.space, idx_id}
        if op.key_cnt == 1 then
            table.insert(args, keys[1])
        elseif op.full_key then
            table.insert(args, key)
        end
        if op.has_iter then
            table.insert(args, idx.iters[math.random(#idx.iters)])
        end
        if op.has_window then
            table.insert(args, math.random(0, ARG_MAX_OFFSET))
        end
        if op.has_window or op.has_limit then
            table.insert(args, math.random(ARG_MAX_LIMIT))
        end
        op.str = op.fmt:format(unpack(args))
    else
        local tuple = gen_tuple()
        local tuple_str = table.concat(tuple, ', ')
        local update = gen_update({ SPACE_FORMAT[3], SPACE_FORMAT[4]})
        if op.subtype == DELETE or op.subtype == UPDATE then
            op.str = op.fmt:format(eng.space, idx_id, key,
                                   update[1], update[2])
        else
            op.str = op.fmt:format(eng.space, tuple_str, update[1], update[2])
        end
    end
    return op
end

-- Choose a random transaction that has not yet been completed.
local function txs_fetch_incomplete(txs)
    local tid = math.random(ARG_TX_CNT)

    for _ = 1, #txs do
        if not txs[tid].committed and not txs[tid].aborted then
            break
        end
        tid = tid % #txs + 1
    end

    if not txs[tid].committed and not txs[tid].aborted then
        if not txs[tid].running then
            txs[tid]:begin()
        end
    else
        return
    end
    return txs[tid]
end

local function tx_execute_stmt(tx, op)
    tx(op)
    if tx.aborted then
        return
    end

    local p = math.random()
    if p < ARG_P_ROLLBACK then
        tx:rollback()
    elseif p < (ARG_P_ROLLBACK + ARG_P_COMMIT) then
        tx:commit()
    end
end

local function txs_stop(txs)
    for tid = 1, #txs do
        if txs[tid].running then
            txs[tid]:commit()
        end
        table.insert(ro_txs_mask, txs[tid].ro)
        table.insert(committed_txs_mask, txs[tid].committed)
        table.insert(bad_dml_txs_mask, txs[tid].bad_dml)
    end
end

local function execute_random_txs()
    local txs = {}
    for tid = 1, ARG_TX_CNT do
        table.insert(txs, tx_new(random_tx_executor.net_box, tid))
    end

    for _ = 1, ARG_STMT_CNT do
        local tx = txs_fetch_incomplete(txs)
        if tx == nil then
            break
        end
        tx_execute_stmt(tx, gen_random_operation(tx))
        if math.random() < ARG_P_GC then
            env_call(('box.internal.memtx_tx_gc(%d)'):format(
                math.random(100)))
        end
        if math.random() < ARG_P_READ_VIEW then
            table.insert(read_views, {prefix = rw_commit_cnt})
            env_call(('open_read_view(%d)'):format(#read_views))
        end
    end
    txs_stop(txs)
end

-- Read the memtx indexes through every read view of the round and close it.
local function read_read_views()
    for i, rv in ipairs(read_views) do
        rv.state = env_call(('read_view_state(%d)'):format(i))
    end
end

-- With no transaction left, the garbage collector must be able to collect
-- all the stories.
local function check_stories_collected()
    env_call('box.internal.memtx_tx_gc(1000000)')
    local stories = env_call('stories_count()')
    if stories ~= 0 then
        dump_repro()
        log.info(('%d stories left after the garbage collection'):format(
            stories))
        os.exit(1)
    end
end

local function cast(v)
    if box.tuple.is(v) then
        local ok, table_value = pcall(function() return v:totable() end)
        if ok and type(table_value) == 'table' then
            return cast(table_value)
        end
        return v
    elseif type(v) == 'table' then
        local result = {}
        for k, val in pairs(v) do
            result[k] = cast(val)
        end
        return result
    end
    return v
end

local function is_less(lhs, rhs)
    for i = 1, 4 do
        local vlhs = lhs[i]
        local vrhs = rhs[i]

        if vlhs == nil and vrhs == nil then
            goto continue
        elseif vlhs == nil then
            return true
        elseif vrhs == nil then
            return false
        elseif vlhs < vrhs then
            return true
        elseif vlhs > vrhs then
            return false
        end
        ::continue::
    end
    return false
end

-- Compare two results. Lists of tuples are compared as sets unless @a ordered
-- is set.
local function is_equal(lhs, rhs, ordered)
    lhs = cast(lhs)
    rhs = cast(rhs)

    if lhs == nil and rhs == nil then
        return true
    end
    if lhs == nil or rhs == nil then
        return false
    end

    local lhs_t = type(lhs)
    local rhs_t = type(rhs)
    if lhs_t ~= rhs_t then
        return false
    end
    assert(not box.tuple.is(lhs))
    if lhs_t ~= 'table' then
        return lhs == rhs
    end

    if type(lhs[1]) == 'table' then
        if type(rhs[1]) ~= 'table' then
            return false
        end
        if not ordered then
            table.sort(lhs, is_less)
            table.sort(rhs, is_less)
        end
    end

    for k, v in ipairs(lhs) do
        if not is_equal(rhs[k], v) then
            return false
        end
    end
    for k, v in ipairs(rhs) do
        if not is_equal(lhs[k], v) then
            return false
        end
    end
    return true
end

local function memtx_idxs()
    for _, eng in ipairs(ENGINES) do
        if eng.name == 'memtx' then
            return eng.idxs
        end
    end
end

-- Compare the read views opened after @a prefix committed read-write
-- transactions with the checker after the serial execution of these.
local function check_read_views(prefix)
    local idxs = memtx_idxs()
    for i, rv in ipairs(read_views) do
        if rv.prefix ~= prefix then
            goto continue
        end
        for idx_id = 0, #idxs - 1 do
            local got = consistency_checker:eval(
                ('return box.space.m.index[%d]:select({}, ' ..
                 '{fullscan = true})'):format(idx_id))
            local ordered = idxs[idx_id + 1].type == 'TREE'
            if not is_equal(rv.state[idx_id + 1], got, ordered) then
                dump_repro()
                dump_serial()
                local msg = ('read view %d of index %d of space m differs ' ..
                             'from the serial state after %d read-write ' ..
                             'transactions:\nexpected:\n%s\ngot:\n%s\n'):format(
                             i, idx_id, prefix,
                             yaml.encode(rv.state[idx_id + 1]),
                             yaml.encode(got))
                log.info(msg)
                os.exit(1)
            end
        end
        ::continue::
    end
end

-- A vinyl transaction is re-executed in a transaction of the checker, so that
-- the reads keeping a state across its statements see its own writes. A memtx
-- transaction cannot span the requests of a stream without MVCC, so it is
-- re-executed statement by statement.
local function try_apply_tx(tx_operations, rw)
    local first = tx_operations[1]
    local stream
    if first ~= nil and first.eng == 'vinyl' then
        stream = consistency_checker.net_box:new_stream()
        stream:begin()
        if rw then
            table.insert(serial, {tid = first.tid, str = 'box.begin()'})
        end
    end
    local function finish(ok)
        if stream == nil then
            return
        end
        if ok then
            stream:commit()
        else
            stream:rollback()
        end
        if rw then
            table.insert(serial, {tid = first.tid, str = 'box.commit()'})
        end
    end
    for i, operation in ipairs(tx_operations) do
        if not operation.ok then
            goto continue
        end

        if rw then
            table.insert(serial, operation)
        end

        local _, res = pcall(function()
            local code = 'return ' .. operation.str
            if stream ~= nil then
                return stream:eval(code)
            end
            return consistency_checker:eval(code)
        end)
        if not is_equal(res, operation.res, operation.ordered) then
            if not rw then
                finish(false)
                return false
            end
            dump_repro()
            dump_serial()
            local msg = ('failed to serialize read-write transaction %d: ' ..
                'discrepancy found on operation #%d \'%s\':\n' ..
                'expected result:\n' ..
                '%s\n' ..
                'got result:\n' ..
                '%s\n'):format(operation.tid, i, operation.str,
                yaml.encode(operation.res), yaml.encode(res))
            log.info(msg)
            os.exit(1)
        end
        ::continue::
    end
    finish(true)
    return true
end

local function try_serialize()
    local rw_txs = {}
    local ro_txs = {}

    local tx_operations = {}
    for _ = 1, ARG_TX_CNT do
        table.insert(tx_operations, {})
    end

    for i = 1, #stmts do
        local stmt = stmts[i]
        if stmt.type == ENV or not committed_txs_mask[stmt.tid] then
            goto continue
        end

        if stmt.str:startswith('box.begin') and committed_txs_mask[stmt.tid] and
           ro_txs_mask[stmt.tid] and not bad_dml_txs_mask[stmt.tid] then
            table.insert(ro_txs, stmt.tid)
        end

        if stmt.str == 'box.commit()' and committed_txs_mask[stmt.tid] and
           not ro_txs_mask[stmt.tid] then
            table.insert(rw_txs, stmt.tid)
        end

        if stmt.type ~= TCL then
            table.insert(tx_operations[stmt.tid], stmt)
        end
        ::continue::
    end

    -- First of all, try to serialize read-only transactions.
    for i, tid in ipairs(ro_txs) do
        if try_apply_tx(tx_operations[tid], false) then
            ro_txs[i] = nil
        end
    end
    check_read_views(0)

    for prefix, rw_tid in ipairs(rw_txs) do
        -- Try to serialize read-write transactions.
        try_apply_tx(tx_operations[rw_tid], true)
        -- Afterwards, try to serialize read-only transactions again.
        for i, ro_tid in pairs(ro_txs) do
            if try_apply_tx(tx_operations[ro_tid], false) then
                ro_txs[i] = nil
            end
        end
        check_read_views(prefix)
    end

    if next(ro_txs) ~= nil then
        local failed_ro_set = {}
        for _, tid in pairs(ro_txs) do
            table.insert(failed_ro_set, tid)
        end
        dump_repro()
        dump_serial()
        local msg = 'failed to serialize the following read-only ' ..
            'transactions: ' .. table.concat(failed_ro_set, ', ')
        log.info(msg)
        os.exit(1)
    end
end

-- The contents of every index of every space, read with @a eval.
local function space_states(eval)
    local states = {}
    for _, eng in ipairs(ENGINES) do
        states[eng.space] = {}
        for idx_id = 0, #eng.idxs - 1 do
            states[eng.space][idx_id] = eval(
                ('box.space.%s.index[%d]:select({}, ' ..
                 '{fullscan = true})'):format(eng.space, idx_id))
        end
    end
    return states
end

-- Compare the spaces of the executor at the end of the round with the spaces
-- of the checker after the serial execution of the committed transactions.
local function check_final_state(expected)
    local got = space_states(function(str)
        return consistency_checker:eval('return ' .. str)
    end)
    for _, eng in ipairs(ENGINES) do
        for idx_id = 0, #eng.idxs - 1 do
            local tuples = expected[eng.space][idx_id]
            local ordered = eng.idxs[idx_id + 1].type == 'TREE'
            if not is_equal(tuples, got[eng.space][idx_id], ordered) then
                dump_repro()
                dump_serial()
                local msg = ('final state of index %d of space %s differs ' ..
                             'from the serial one:\n' ..
                             'expected:\n%s\ngot:\n%s\n'):format(
                             idx_id, eng.space, yaml.encode(tuples),
                             yaml.encode(got[eng.space][idx_id]))
                log.info(msg)
                os.exit(1)
            end
        end
    end
end

local function open_files()
    local repro_file_name = 'repro.lua'
    local repro_file_path = fio.pathjoin(random_tx_executor.workdir, '..',
                                         repro_file_name)
    local err
    repro_file, err = fio.open(repro_file_path,
                               {'O_WRONLY', 'O_CREAT', 'O_TRUNC'},
                               {'S_IRUSR', 'S_IWUSR'})
    if repro_file == nil then
        log.info('fio.open failed: ' .. err)
        os.exit(1)
    end

    local serial_file_name = 'serial.lua'
    local serial_file_path = fio.pathjoin(consistency_checker.workdir, '..',
        serial_file_name)
    serial_file, err = fio.open(serial_file_path,
                               {'O_WRONLY', 'O_CREAT', 'O_TRUNC'},
                               {'S_IRUSR', 'S_IWUSR'})
    if serial_file == nil then
        log.info('fio.open failed: ' .. err)
        os.exit(1)
    end
end

local function close_files()
    repro_file:close()
    serial_file:close()
end

local function fiber_set_max_slice()
    require('fiber').set_max_slice({warn = 6000, err = 6000})
end

local function setup_test()
    if fio.path.exists(ARG_TEST_DIR) then
        fio.rmtree(ARG_TEST_DIR)
    end
    fio.mkdir(ARG_TEST_DIR)

    math.randomseed(ARG_SEED)
    log.info('Random seed: %d', ARG_SEED)

    random_tx_executor = server:new({
        alias = 'random_tx_executor',
        workdir = fio.pathjoin(ARG_TEST_DIR, 'random_tx_executor'),
        box_cfg = {
            memtx_use_mvcc_engine = true,
        },
    })
    consistency_checker = server:new({
        alias = 'consistency_checker',
        workdir = fio.pathjoin(ARG_TEST_DIR, 'consistency_checker'),
        box_cfg = {
            memtx_use_mvcc_engine = false,
        },
    })
    random_tx_executor:start()
    consistency_checker:start()
    random_tx_executor:exec(fiber_set_max_slice)
    consistency_checker:exec(fiber_set_max_slice)
    random_tx_executor:eval(HELPERS_CODE)
    consistency_checker:eval(HELPERS_CODE)

    open_files()
end

local function shutdown_test()
    close_files()
    random_tx_executor:drop()
    consistency_checker:drop()
    os.exit(0)
end

local function run_test()
    setup_test()

    local schema_creation_code = generate_schema_code()

    for _ = 1, ARG_N_ROUNDS do
        random_tx_executor:eval(schema_creation_code)
        prefill_code = generate_prefill_code()
        random_tx_executor:eval(prefill_code)

        stmts = {}
        rw_commit_cnt = 0
        read_views = {}
        committed_txs_mask = {}
        ro_txs_mask = {}
        bad_dml_txs_mask = {}
        execute_random_txs()
        read_read_views()
        check_stories_collected()
        local final_state = space_states(env_call)
        random_tx_executor:exec(function()
            box.space.m:drop()
            box.space.v:drop()
        end)

        consistency_checker:eval(schema_creation_code)
        consistency_checker:eval(prefill_code)

        serial = {}
        try_serialize()
        check_final_state(final_state)
        consistency_checker:exec(function()
            box.space.m:drop()
            box.space.v:drop()
        end)
    end

    shutdown_test()
end

run_test()
