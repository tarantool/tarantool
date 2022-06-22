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

Usage: tarantool <path_to_luatest_executable> ./mvcc_test.lua

[1]: https://www.usenix.org/conference/osdi23/presentation/jiang
[2]: https://dl.acm.org/doi/10.1109/ICSE48619.2023.00101
]]

local cluster = require('luatest.replica_set')
local fio = require('fio')
local json = require('json')
local log = require('log')
local t = require('luatest')
local yaml = require('yaml')

local g = t.group()

-- Global test parameters.

-- Random seed.
local SEED = os.getenv("RANDOM_SEED")
-- Number of rounds to run.
local N_ROUNDS = 1000
-- Number of transactions in 1 round.
local TX_CNT = 32
-- Number of read-only transactions.
local RO_TX_CNT = 8
-- Number of statements in 1 round.
local STMT_CNT = 320
-- Probability of rollback.
local P_ROLLBACK = 0.05
-- Probability of commit.
local P_COMMIT = 0.1
-- Max random unsigned key (min is 1).
local MAX_KEY = 8
-- Probability of NULL value for nullable keys.
local P_NULL_KEY = 0.1

-- Global test variables.

local repro_file
local serial_file
local stmts
local ro_txs_mask
local committed_txs_mask
local bad_dml_txs_mask
local serial

-- Transaction operation types.
local DML = 0
local DQL = 1
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
local space_format = {
    {'1', type = 'uint'},
    {'2', type = 'uint'},
    {'3', type = 'uint', is_nullable = true},
    {'4', type = 'uint', is_nullable = true}
}

local function create_generic_idx_meta(fields)
    local ro_ops = {
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
                  '{iterator = "%s", fullscan = true})',
        },
        {
            type = DQL,
            subtype = GET,
            fmt = 'box.space.%s.index[%d]:get{%s, %s}',
        },
    }
    local ops = {
        unpack(ro_ops),
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
                  '{{"=", 3, %s}, {"=", 4, %s}})',
        },
    }
    return {
        fields = fields,
        ro_ops = ro_ops,
        ops = ops,
    }
end

local function create_tree_idx_meta(fields)
    local tree_idx_meta = create_generic_idx_meta(fields)
    tree_idx_meta.iters = {'EQ', 'REQ', 'GT', 'GE', 'LT', 'LE'}
    local additional_ops = {
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 0,
            fmt = 'box.space.%s.index[%d]:select({}, ' ..
                  '{iterator = "%s", fullscan = true})',
        },
        {
            type = DQL,
            subtype = SELECT,
            key_cnt = 1,
            fmt = 'box.space.%s.index[%d]:select({%s}, ' ..
                  '{iterator = "%s", fullscan = true})',
        },
    }
    for _, op in ipairs(additional_ops) do
        table.insert(tree_idx_meta.ro_ops, op)
        table.insert(tree_idx_meta.ops, op)
    end
    return tree_idx_meta
end

local function create_hash_idx_meta(fields)
    local hash_idx_meta = create_generic_idx_meta(fields)
    hash_idx_meta.iters = {'EQ'}
    return hash_idx_meta
end

-- Metadata of tested engines.
local engines = {
    {
        name = 'memtx',
        space = 'm',
        idxs = {
            create_tree_idx_meta({{is_nullable = false},
                                  {is_nullable = false}}),
            create_tree_idx_meta({{is_nullable = true},
                                  {is_nullable = false}}),
            create_tree_idx_meta({{is_nullable = false},
                                  {is_nullable = true}}),
            create_tree_idx_meta({{is_nullable = true},
                                  {is_nullable = true}}),
            create_hash_idx_meta({{is_nullable = false},
                                  {is_nullable = false}}),
            create_tree_idx_meta({{is_nullable = false},
                                  {is_nullable = false}}),
        }
    },
    {
        name = 'vinyl',
        space = 'v',
        idxs = {
            create_tree_idx_meta({{is_nullable = false},
                                  {is_nullable = false}}),
            create_tree_idx_meta({{is_nullable = true},
                                  {is_nullable = false}}),
            create_tree_idx_meta({{is_nullable = false},
                                  {is_nullable = true}}),
            create_tree_idx_meta({{is_nullable = true},
                                  {is_nullable = true}}),
        }
    },
}

local function generate_txn_repro_stmt(stmt)
    return ("tx%d('%s') -- %s\n"):format(stmt.tid, stmt.str,
                                         json.encode(stmt.res))
end

local function generate_txn_serial_stmt(stmt)
    return ("%s -- tx%d: %s\n"):format(stmt.str, stmt.tid,
                                       json.encode(stmt.res))
end

local INIT_CODE =
[[os.execute('rm -rf *.snap *.xlog *.vylog 512 513')

local ffi = require('ffi')
local fiber = require('fiber')
local json = require('json')
local log = require('log')
local txn_proxy = require('test.box.lua.txn_proxy')

box.cfg{memtx_use_mvcc_engine = %s}
fiber.set_max_slice({warn = 6000, err = 6000})

]]

local function generate_init_code(memtx_use_mvcc_engine)
    return (INIT_CODE):format(memtx_use_mvcc_engine and 'true' or 'false')
end

local SCHEMA_CODE =
[[box.schema.func.create('func',
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
box.space.m:create_index('s4', {type = 'HASH', parts = {
    {1, 'uint'},
    {2, 'uint'}}})
box.space.m:create_index('s5', {parts = {
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

]]

local function generate_schema_code()
    return SCHEMA_CODE
end

local function generate_txn_proxy(id)
    return ("local tx%d = txn_proxy:new()\n"):format(id)
end

local EXIT_CODE =
[[
os.exit()

]]

local function dump_repro()
    repro_file:write(generate_init_code(true) ..
                     generate_schema_code())
    for i = 1, TX_CNT do
        repro_file:write(generate_txn_proxy(i))
    end
    repro_file:write("\n")
    for _, stmt in ipairs(stmts) do
        repro_file:write(generate_txn_repro_stmt(stmt))
    end
    repro_file:write(EXIT_CODE)
end

local function dump_serial()
    serial_file:write(generate_init_code(false) ..
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
    if ok and operation.type == DML then tx.ro = false end
    if not ok then
        if res.message == 'Transaction has been aborted by conflict' then
            -- TODO(gh-11397): add probability for keeping this transaction.
            tx:rollback()
            return ok, res
        end
        -- Set `bad_dml` to filter this transaction from RO ones later on.
        if operation.type == DML then tx.bad_dml = true end
    end
    if tx._strm._conn.state == 'error' then
        dump_repro()
        t.fail(('connection in error state: %s'):format(tx._strm._conn.error))
    end
    return ok, res
end

local function tx_begin(tx)
    t.fail_if(tx.running or tx.committed or tx.aborted,
              'internal test error: cannot start transaction')
    tx{
        tid  = tx.id,
        type = TCL,
        str  = 'box.begin()',
    }
    tx.running = true
end

local function tx_rollback(tx)
    t.fail_if(not tx.running or tx.committed or tx.aborted,
              'internal test error: cannot rollback transaction')
    local ok, res = tx{
        tid  = tx.id,
        type = TCL,
        str  = 'box.rollback()',
    }
    if not ok then t.fail('`stream:rollback` failed: ' .. res[1].error) end
    tx.running = false
    tx.aborted = true
end

local function tx_commit(tx)
    t.fail_if(not tx.running or tx.committed or tx.aborted,
              'internal test error: cannot commit transaction')
    local ok, err = tx{
                tid  = tx.id,
                type = TCL,
                str  = 'box.commit()',
    }
    if not ok then
        t.fail_if(err.message ~= 'Transaction has been aborted by conflict',
                  '`stream:commit` failed unexpectedly: ' .. err)
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
            rollback = tx_rollback,
            commit   = tx_commit,
        },
        __call = tx_call,
    }
    return setmetatable({
        id        = id,
        _strm     = conn:new_stream(),
        running   = false,
        committed = false,
        aborted   = false,
        ro        = true,
        bad_dml   = false,
    }, mt)
end

local function rand_number()
    return math.random(MAX_KEY)
end

local function gen_field(field)
    if field.is_nullable and math.random() <= P_NULL_KEY then
        return "box.NULL"
    end
    return rand_number()
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
    for _, field in ipairs(space_format) do
        table.insert(tuple, gen_field(field))
    end
    return tuple
end

local function gen_update(fields)
    return {('{"=", 3, %s}'):format(gen_field(fields[1])),
            ('{"=", 4, %s}'):format(gen_field(fields[2]))}
end

local function gen_rand_operation(ro)
    local eng = engines[math.random(#engines)]
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
        local update = gen_update({space_format[3], space_format[4]})
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
    local tid = math.random(TX_CNT)

    for _ = 1, #txs do
        if not txs[tid].committed and not txs[tid].aborted then break end
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

local function tx_gen_stmt(tx, ro)
    tx(gen_rand_operation(ro))
    if tx.aborted then
        return
    end

    local p = math.random()
    if p < P_ROLLBACK then
        tx:rollback()
    elseif p < (P_ROLLBACK + P_COMMIT) then
        tx:commit()
    end
end

local function txs_stop(txs)
    for tid = 1, #txs do
        if txs[tid].running then txs[tid]:commit() end
        table.insert(ro_txs_mask, txs[tid].ro)
        table.insert(committed_txs_mask, txs[tid].committed)
        table.insert(bad_dml_txs_mask, txs[tid].bad_dml)
    end
end

local function gen_stmts()
    local txs = {}
    for tid = 1, TX_CNT do
        table.insert(txs, tx_new(g.mvcc.net_box, tid))
    end

    for _ = 1, STMT_CNT do
        local tx = txs_fetch_incomplete(txs)
        if tx == nil then break end

        tx_gen_stmt(tx, tx.id <= RO_TX_CNT)
    end
    txs_stop(txs)
end

local function cast(v)
    if type(v) == 'cdata' then
        local ok, table_value = pcall(function() return v:totable() end)
        if ok and type(table_value) == "table" then
            return cast(table_value)
        end
        return v
    elseif type(v) == 'table' then
        local result = {}
        for k, val in pairs(v) do
            result[k] = cast(val)
        end
        return result
    else
        return v
    end
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

    if lhs == nil and rhs == nil then return true end
    if lhs == nil or rhs == nil then return false end

    local lhs_t = type(lhs)
    local rhs_t = type(rhs)
    if lhs_t ~= rhs_t then return false end
    assert(lhs_t ~= 'cdata')
    if lhs_t ~= 'table' then return lhs == rhs end

    if type(lhs[1]) == 'table' then
        if type(rhs[1]) ~= 'table' then
            return false
        end
        table.sort(lhs, is_less)
        table.sort(rhs, is_less)
    end

    for k, v in ipairs(lhs) do
        if not is_equal(rhs[k], v) then return false end
    end
    for k, v in ipairs(rhs) do
        if not is_equal(lhs[k], v) then return false end
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

        local _, res = pcall(g.oracle.eval, g.oracle, 'return ' ..
                             operation.str)
        if not is_equal(res, operation.res) then
            if not rw then
                return false
            end
            dump_repro()
            dump_serial()
            local msg = ('failed to serialize read-write transaction %d: ' ..
                'discrepancy found on operation #%d "%s":\n' ..
                'expected result:\n' ..
                '%s\n' ..
                'got result:\n' ..
                '%s\n'):format(operation.tid, i, operation.str,
                yaml.encode(operation.res), yaml.encode(res))
            t.fail(msg)
        end
        ::continue::
    end
    return true
end

local function try_serialize()
    local rw_txs = {}
    local ro_txs = {}

    local tx_operations = {}
    for _ = 1, TX_CNT do
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
        if try_apply_tx(tx_operations[tid], false) then ro_txs[i] = nil end
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
        t.fail(msg)
    end
end

local function open_files()
    local repro_file_name = 'repro.lua'
    local repro_file_path = fio.pathjoin(g.mvcc.workdir, '..',
                                         repro_file_name)
    local err
    repro_file, err = fio.open(repro_file_path,
                               {'O_WRONLY', 'O_CREAT', 'O_TRUNC'},
                               {'S_IRUSR', 'S_IWUSR'})
    t.fail_if(repro_file == nil, ("`fio.open` failed: %s"):format(err))

    local serial_file_name = 'serial.lua'
    local serial_file_path = fio.pathjoin(g.oracle.workdir, '..',
        serial_file_name)
    serial_file, err = fio.open(serial_file_path,
                               {'O_WRONLY', 'O_CREAT', 'O_TRUNC'},
                               {'S_IRUSR', 'S_IWUSR'})
    t.fail_if(serial_file == nil, ('`fio.open` failed: %s'):format(err))
end

local function close_files()
    repro_file:close()
    serial_file:close()
end

g.before_all(function()
    math.randomseed(SEED)
    log.info("Random seed: %d", SEED)

    g.cluster = cluster:new{}
    g.oracle = g.cluster:build_and_add_server{
        alias   = 'oracle',
    }
    g.mvcc = g.cluster:build_and_add_server{
        alias   = 'mvcc',
        box_cfg = {
            memtx_use_mvcc_engine = true
        }
    }
    g.cluster:start()
    g.oracle:exec(function()
        require('fiber').set_max_slice({warn = 6000, err = 6000})
    end)
    g.mvcc:exec(function()
        require('fiber').set_max_slice({warn = 6000, err = 6000})
    end)

    open_files()
end)

g.after_all(function()
    close_files()
    g.cluster:drop()
end)

g.test_mvcc = function()
    local schema_creation_code = generate_schema_code()

    for _ = 1, N_ROUNDS do
        g.mvcc:eval(schema_creation_code)

        stmts = {}
        committed_txs_mask = {}
        ro_txs_mask = {}
        bad_dml_txs_mask = {}
        gen_stmts()
        g.mvcc:exec(function()
            box.space.m:drop()
            box.space.v:drop()
        end)

        g.oracle:eval(schema_creation_code)

        serial = {}
        try_serialize()
        g.oracle:exec(function()
            box.space.m:drop()
            box.space.v:drop()
        end)
    end
end
