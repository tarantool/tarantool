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
   help (same as -h)         - print this message
]]):format(DEFAULT_TEST_DIR_NAME, DEFAULT_N_ROUNDS, DEFAULT_TX_CNT,
           DEFAULT_RO_TX_CNT, DEFAULT_STMT_CNT, DEFAULT_P_ROLLBACK,
           DEFAULT_P_COMMIT, DEFAULT_MAX_KEY, DEFAULT_P_NULL_KEY))
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

-- Global test constants.

-- Transaction operation types.
-- Data manipulation language.
local DML = 0
-- Data query language.
local DQL = 1
-- Transaction control language (BEGIN, COMMIT, ROLLBACK).
local TCL = 2

-- DML operation subtypes.
local DELETE = 0
local INSERT = 1
local REPLACE = 2
local UPDATE = 3
local UPSERT = 4

-- DQL operation subtypes.
local SELECT = 0
local GET = 1

-- Format of test space.
local SPACE_FORMAT = {
    {'1', type = 'uint'},
    {'2', type = 'uint'},
    {'3', type = 'uint', is_nullable = true},
    {'4', type = 'uint', is_nullable = true}
}

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
            key_cnt = 2,
            fmt = 'box.space.%s.index[%d]:select({%s, %s}, ' ..
                  '{iterator = \'%s\', fullscan = true})',
        },
        {
            type = DQL,
            subtype = GET,
            fmt = 'box.space.%s.index[%d]:get{%s, %s}',
        },
    }
}
generic_idx_meta.ops = {
        unpack(generic_idx_meta.ro_ops),
        {
            type = DML,
            subtype = UPDATE,
            fmt = 'box.space.%s.index[%d]:update({%s, %s}, ' ..
                  '{%s, %s})',
        },
        {
            type = DML,
            subtype = DELETE,
            fmt = 'box.space.%s.index[%d]:delete{%s, %s}',
        },
        {
            type = DML,
            subtype = INSERT,
            fmt = 'box.space.%s:insert{%d, %d, %s, %s}',
        },
        {
            type = DML,
            subtype = REPLACE,
            fmt = 'box.space.%s:replace{%d, %d, %s, %s}',
        },
        {
            type = DML,
            subtype = UPSERT,
            fmt = 'box.space.%s:upsert({%d, %d, %s, %s}, ' ..
                  '{{\'=\', 3, %s}, {\'=\', 4, %s}})',
        },
}

local tree_idx_meta = table.copy(generic_idx_meta)
tree_idx_meta.iters = {'EQ', 'REQ', 'GT', 'GE', 'LT', 'LE'}
local tree_idx_meta_additional_ops = {
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 0,
            fmt = 'box.space.%s.index[%d]:select({}, ' ..
                  '{iterator = \'%s\', fullscan = true})',
        },
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 1,
            fmt = 'box.space.%s.index[%d]:select({%s}, ' ..
                  '{iterator = \'%s\', fullscan = true})',
        },
    }
for _, op in ipairs(tree_idx_meta_additional_ops) do
    table.insert(tree_idx_meta.ro_ops, op)
    table.insert(tree_idx_meta.ops, op)
end

local hash_idx_meta = table.copy(generic_idx_meta)
hash_idx_meta.iters = {'EQ'}

local function create_idx_meta(fields, type)
    type = type or 'TREE'
    local res
    if type == 'TREE' then
        res = table.copy(tree_idx_meta)
    else
        res = table.copy(hash_idx_meta)
    end
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
-- Server used for executing random transactions with MVCC enabled.
local random_tx_executor
-- Server used for checking consistency of executed transactions with MVCC
-- disabled.
local consistency_checker

local function generate_txn_repro_stmt(stmt)
    return ('tx%d(\'%s\') -- %s\n'):format(stmt.tid, stmt.str,
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
    {body = 'function(tuple) return tuple end', is_deterministic = true,
     is_sandboxed = true, if_not_exists = true})
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
]]

local function generate_schema_code()
    return SCHEMA_CODE
end

local function generate_txn_proxy(id)
    return ('local tx%d = txn_proxy:new()\n'):format(id)
end

local EXIT_CODE = [[
os.exit()

]]

local function dump_repro()
    repro_file:write(generate_init_code('repro', true) ..
                     generate_schema_code())
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
                      generate_schema_code())
    for _, stmt in ipairs(serial) do
        serial_file:write(generate_txn_serial_stmt(stmt))
    end
    serial_file:write(EXIT_CODE)
end

local function tx_call(tx, operation)
    local ok, res = pcall(tx._strm.eval, tx._strm, 'return ' .. operation.str)
    table.insert(stmts, {
        tid  = tx.id,
        type = operation.type,
        str  = operation.str,
        ok   = ok,
        res  = res,
    })
    if ok and operation.type == DML then
        tx.ro = false
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

local function tx_begin(tx)
    assert(not tx.running and not tx.committed and not tx.aborted)
    tx({
        tid  = tx.id,
        type = TCL,
        str  = 'box.begin()',
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
        id        = id,
        ro        = true,
        running   = false,
        _strm     = conn:new_stream(),
    }, mt)
end

local function random_number()
    return math.random(ARG_MAX_KEY)
end

local function gen_field(field)
    if field.is_nullable and math.random() <= ARG_P_NULL_KEY then
        return 'box.NULL'
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

local function gen_update(fields)
    return {('{\'=\', 3, %s}'):format(gen_field(fields[1])),
            ('{\'=\', 4, %s}'):format(gen_field(fields[2]))}
end

local function gen_random_operation(ro)
    local eng = ENGINES[math.random(#ENGINES)]
    local idx_id = math.random(#eng.idxs) - 1
    local idx = eng.idxs[idx_id + 1]
    local op = ro and idx.ro_ops[math.random(#idx.ro_ops)] or
               idx.ops[math.random(#idx.ops)]
    local keys = gen_key(idx.fields)
    if op.type == DQL then
        if op.subtype == SELECT then
            local iter = idx.iters[math.random(#idx.iters)]
            if op.key_cnt == 0 then
                op.str = op.fmt:format(eng.space, idx_id, iter)
            elseif op.key_cnt == 1 then
                op.str = op.fmt:format(eng.space, idx_id, keys[1], iter)
            elseif op.key_cnt == 2 then
                op.str = op.fmt:format(eng.space, idx_id, keys[1], keys[2],
                                       iter)
            end
        elseif op.subtype == GET then
            op.str = op.fmt:format(eng.space, idx_id, keys[1], keys[2])
        end
    else
        local tuple = gen_tuple()
        local update = gen_update({ SPACE_FORMAT[3], SPACE_FORMAT[4]})
        if op.subtype == DELETE or op.subtype == UPDATE then
            op.str = op.fmt:format(eng.space, idx_id, keys[1], keys[2],
                                   update[1], update[2])
        else
            op.str = op.fmt:format(eng.space, tuple[1], tuple[2], tuple[3],
                                   tuple[4], update[1], update[2])
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
        tx_execute_stmt(tx, gen_random_operation(tx.id <= ARG_RO_TX_CNT))
    end
    txs_stop(txs)
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

local function is_equal(lhs, rhs)
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
        table.sort(lhs, is_less)
        table.sort(rhs, is_less)
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

local function try_apply_tx(tx_operations, rw)
    for i, operation in ipairs(tx_operations) do
        if not operation.ok then
            goto continue
        end

        if rw then
            table.insert(serial, operation)
        end

        local _, res = pcall(function()
            return consistency_checker:eval('return ' .. operation.str)
        end)
        if not is_equal(res, operation.res) then
            if not rw then
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
        if not committed_txs_mask[stmt.tid] then
            goto continue
        end

        if stmt.str:find('begin') and committed_txs_mask[stmt.tid] and
           ro_txs_mask[stmt.tid] and not bad_dml_txs_mask[stmt.tid] then
            table.insert(ro_txs, stmt.tid)
        end

        if stmt.str:find('commit') and committed_txs_mask[stmt.tid] and
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

    for _, rw_tid in ipairs(rw_txs) do
        -- Try to serialize read-write transactions.
        try_apply_tx(tx_operations[rw_tid], true)
        -- Afterwards, try to serialize read-only transactions again.
        for i, ro_tid in pairs(ro_txs) do
            if try_apply_tx(tx_operations[ro_tid], false) then
                ro_txs[i] = nil
            end
        end
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

        stmts = {}
        committed_txs_mask = {}
        ro_txs_mask = {}
        bad_dml_txs_mask = {}
        execute_random_txs()
        random_tx_executor:exec(function()
            box.space.m:drop()
            box.space.v:drop()
        end)

        consistency_checker:eval(schema_creation_code)

        serial = {}
        try_serialize()
        consistency_checker:exec(function()
            box.space.m:drop()
            box.space.v:drop()
        end)
    end

    shutdown_test()
end

run_test()
