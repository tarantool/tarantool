test_run = require('test_run').new()
netbox = require('net.box')
console = require('console')
yaml = require('yaml')
json = require('json')
--------------------------------------------------------------------------------

-- False iff only successful txns included in reproducer
g_is_reproduce_all = true

-- True iff only meaningful txns included in reproducer
g_is_reproduce_zip = true

-- Number of test rounds
g_nrounds = 64

-- Number of all and read only transactions
g_ntx, g_nro = 20, 8

-- Number of statements
g_cnt = 500

-- Space format table
g_format = {\
    {'f1', type='unsigned'},\
    {'f2', type='string'},\
    {'f3', type='double', is_nullable=true},\
};

-- Array of all indexes (PK type must be randomized on every iteration)
g_indarr = {\
    [0] = {type='none', parts={{1, 'unsigned'}}},\
    [1] = {type='tree', parts={{1, 'unsigned'}}},\
    [2] = {type='hash', parts={{1, 'unsigned'}}},\
    [3] = {type='tree', parts={{2, 'string'}}, unique=false},\
    [4] = {type='tree', parts={{2, 'string'}, {3, 'double'}}},\
    [5] = {type='hash', parts={{1, 'unsigned'}, {2, 'string'}}},\
};

-- START HOST
test_run:cmd('create server mvcc_mass with script="box/tx_man.lua"')
test_run:cmd('start server mvcc_mass')
--------------------------------------------------------------------------------

-- START HOST INIT
test_run:cmd('switch mvcc_mass')
box.schema.user.grant('guest','read,write,execute,create,drop','universe')

txn_proxy = require('txn_proxy')

-- simultaneous txns
sim = box.schema.space.create('s', {engine='memtx', if_not_exists=true})

-- START HOST API DEFINITIONS
test_run:cmd('setopt delimiter ";"')
g_seed = os.time();
math.randomseed(g_seed);

-- Probability of the rollback
g_rollback_prob = 0.05;

-- Probability of the commit
g_commit_prob = 0.2;

-- Probability of the null field (if nullable)
g_null_field_prob = 0.15

-- Maximum random key value (min is 0)
g_max_key_val = 10;

-- Maximum random value string length (min is 0)
g_max_val_len = 3;

-- Alphabet size for value string (from 1 to 26)
g_alph_size = 3;

function refresh_sim_space(format, indarr)
    sim:drop()
    sim = box.schema.space.create('s', {engine='memtx', if_not_exists=true})
    sim:format(format)
    local nind = #indarr
    for i = 0, nind do
        sim:create_index('IDX'..i, indarr[i])
    end
end;

function txn_begin(txn)
    if txn.running or txn.commited or txn.cancelled then
        return error('Can\'t begin txn')
    end
    txn.handle:begin()
    txn.running = true
    return true
end;

function txn_rollback(txn)
    if not txn.running or txn.commited or txn.cancelled then
        return error('Can\'t rollback txn')
    end
    txn.handle:rollback()
    txn.running = false
    txn.cancelled = true
    return true
end;

function txn_commit(txn)
    if not txn.running or txn.commited or txn.cancelled then
        return error('Can\'t commit txn')
    end
    local res = txn.handle:commit()
    if res and res[1] and res[1]['error'] then
        if res[1]['error'] ~= 'Transaction has been aborted by conflict' then
            return error('Unknown error while commit')
        end
        txn.running = false
        txn.cancelled = true
        return false
    end
    txn.running = false
    txn.commited = true
    return true
end;

function txn_close(txn)
    if not txn.commited and not txn.cancelled then
        return error('Can\'t close txn')
    end
    txn.handle:close()
    return true
end;

function txn_new()
    local data = {
        is_ro = true, has_bad_rw = false, handle = txn_proxy.new(),
        running = false, commited = false, cancelled = false,
    }
    local meta = {
        __index={
            begin=txn_begin, rollback=txn_rollback, commit=txn_commit,
            close=txn_close
        }
    }
    return setmetatable(data, meta)
end;

function get_str_tuple(tuple)
    local len = #tuple
    local res = ''
    for fieldno = 1, len do
        res = res..tuple[fieldno]..', '
    end

    return res
end;

function get_str_tuple_key(tuple, index)
    local parts = index.parts
    local len = #tuple
    local res = ''
    local nparts = #parts
    for part_idx = 1, nparts do
        local fieldno = parts[part_idx].fieldno
        if fieldno and fieldno <= len then
            res = res..tuple[fieldno]..', '
        end
    end

    return res
end;

function get_str_tuple_update(tuple)
    local len = #tuple
    local fieldno = math.random(len)
    local op = '='
    local arg = tuple[fieldno]
    return '{{\''..op..'\', '..fieldno..', '..arg..'}}'
end;

function gen_rand_uint(maxval)
    return math.random(maxval)
end;

function gen_rand_str(maxlen)
    local asciis = {}
    local len = math.random(maxlen)
    for _ = 1, len do
-- `g_alph_size` characters from 'a' (== ASCII 97)
        table.insert(asciis, math.random(97, 97 + g_alph_size - 1))
    end
    return '\''..string.char(unpack(asciis))..'\''
end;

function gen_rand_double()
    return math.random()
end;

function gen_rand_tuple(maxval, maxlen)
    if math.random() < g_null_field_prob then
        return {gen_rand_uint(maxval), gen_rand_str(maxlen)}
    end

    return {gen_rand_uint(maxval), gen_rand_str(maxlen), gen_rand_double()}
end;

function gen_rand_ind()
-- index id's are by default sequentially generated starting from 0
    return math.random(#sim.index + 1) - 1
end;

function exec_rand_stmt(txns, t, is_ro)
    local cmds = {
        'select', 'selall',
-- end ro
        'delete', 'update',
-- end ind
        'insert', 'replace',
-- end all
    }

    local nro = 2
    local nind = 4
    local nall = #cmds

    local cmd_num = is_ro and math.random(nro) or math.random(nall)
    local cmd_is_ro = cmd_num <= nro
    local cmd_is_ind = cmd_num <= nind

    local cmd = cmds[cmd_num]
    local op = ''
    local ind = cmd_is_ind and gen_rand_ind() or nil

    if cmd == 'select' then
        local keytup = gen_rand_tuple(g_max_key_val, g_max_val_len)
        local strkey = get_str_tuple_key(keytup, sim.index[ind])
        op = string.format('select{%s}', strkey)
    elseif cmd == 'selall' then
        op = string.format('select{}')
    elseif cmd == 'delete' then
        local keytup = gen_rand_tuple(g_max_key_val, g_max_val_len)
        local strkey = get_str_tuple_key(keytup, sim.index[ind])
        op = string.format('delete{%s}', strkey)
    elseif cmd == 'update' then
        local keytup = gen_rand_tuple(g_max_key_val, g_max_val_len)
        local strkey = get_str_tuple_key(keytup, sim.index[ind])
        local updtup = gen_rand_tuple(g_max_key_val, g_max_val_len)
        local strupd = get_str_tuple_update(updtup)
        op = string.format('update({%s}, %s)', strkey, strupd)
    elseif cmd == 'insert' then
        local tup = gen_rand_tuple(g_max_key_val, g_max_val_len)
        local strtuple = get_str_tuple(tup)
        op = string.format('insert{%s}', strtuple)
    elseif cmd == 'replace' then
        local tup = gen_rand_tuple(g_max_key_val, g_max_val_len)
        local strtuple = get_str_tuple(tup)
        op = string.format('replace{%s}', strtuple)
    else
        error('op \''..cmd..'\' not defined')
    end

    local res = cmd_is_ind and
        txns[t].handle('sim.index['..ind..']:'..op) or
        txns[t].handle('sim:'..op)

    if not cmd_is_ro then
        if res[1] and res[1]['error'] then
            txns[t].has_bad_rw = true
        else
            txns[t].is_ro = false
        end
    end

    if res[1] and res[1]['error'] ==
        'Transaction has been aborted by conflict' then
        return
    end

-- execute op for space 's'
    return {t=t, ind=ind, op=op, res=res}
end;

function txns_try_get_t(txns, stmts)
    local ntx = #txns
    local t = math.random(ntx)
-- Try to find not finished txn
    for i = 1, ntx do
        if not txns[t].commited and not txns[t].cancelled then break end
        t = t % ntx + 1
    end

-- If found one than start it else we are fully done
    if not txns[t].commited and not txns[t].cancelled then
        if not txns[t].running then
            txns[t]:begin()
            table.insert(stmts, {t=t, op='begin'})
        end
    else
        return
    end

    return t
end;

function txns_add_stmt(txns, stmts, t, is_ro)
    local stmt = exec_rand_stmt(txns, t, is_ro)
-- Check that txn wasn't cancelled by stmt
    if stmt then
        table.insert(stmts, stmt)

        local dice = math.random()
        if dice < g_rollback_prob then
            txns[t]:rollback()
            table.insert(stmts, {t=t, op='rollback'})
        elseif dice < (g_rollback_prob + g_commit_prob) then
            if txns[t]:commit() then
                table.insert(stmts, {t=t, op='commit'})
            else
                table.insert(stmts, {t=t, op='rollback'})
            end
        end
    else
        txns[t]:rollback()
        table.insert(stmts, {t=t, op='rollback'})
    end
end;

function txns_stop(txns, stmts, t)
    if txns[t].running then
        if txns[t]:commit() then
            table.insert(stmts, {t=t, op='commit'})
        else
            table.insert(stmts, {t=t, op='rollback'})
        end
    end
    txns[t]:close()
end;

function gen_statements(ntx, nro, cnt)
    local stmts = {}
    local ro_mask = {}
    local ok_mask = {}
    local bad_rw_mask = {}
    local txns = {}

    for _ = 1, ntx do table.insert(txns, txn_new()) end

    for _ = 1, cnt do
        local t = txns_try_get_t(txns, stmts)
        if not t then break end

        local is_ro = (t <= nro)
        txns_add_stmt(txns, stmts, t, is_ro)
    end

    for t = 1, ntx do
        txns_stop(txns, stmts, t)
        table.insert(ro_mask, txns[t].is_ro)
        table.insert(ok_mask, txns[t].commited)
        table.insert(bad_rw_mask, txns[t].has_bad_rw)
    end

    return stmts, ro_mask, ok_mask, bad_rw_mask, g_seed
end;

test_run:cmd('setopt delimiter ""');
-- END HOST API DEFINITIONS

test_run:cmd('switch default')
-- END HOST INIT
--------------------------------------------------------------------------------

-- START GUEST API DEFINITIONS
test_run:cmd('setopt delimiter ";"')

function is_equal(lhs, rhs)
    if not lhs and not rhs then return true end
    if not (lhs and rhs) then return false end

    local lhs_t = box.tuple.is(lhs) and 'table' or type(lhs)
    local rhs_t = box.tuple.is(rhs) and 'table' or type(rhs)

    if lhs_t ~= rhs_t then return false end

    if lhs_t ~= 'table' then
        return lhs == rhs
    end

    for k, v in pairs(lhs) do
        if not is_equal(rhs[k], v) then return false end
    end

    for k, v in pairs(rhs) do
        if not is_equal(lhs[k], v) then return false end
    end

    return true
end;

function try_apply_txn(stmts)
    local nstmts = #stmts
    for i = 1, nstmts do
        local stmt = stmts[i]
        local yamlres = stmt.ind and
            console.eval('box.space.s.index['..stmt.ind..']:'..stmt.op) or
            console.eval('box.space.s:'..stmt.op)
        local res = yaml.decode(yamlres)

-- we need to sort select{} results because of the
-- undefined order of tuples in case of hash index
        if string.sub(stmt.op, 1, 6) == 'select' then
            local pred = function (lhs, rhs) return lhs[1] < rhs[1] end
            if #res > 0 then table.sort(res[1], pred) end
            if #stmt.res > 0 then table.sort(stmt.res[1], pred) end
        end

        if not is_equal(res, stmt.res) then
            return false, 'TXN #'..stmt.t..
                          (stmt.ind and ' INDEX_ID: '..stmt.ind or '')..
                          ' OP '..stmt.op..
                          ' FAILED: expected '..json.encode(res)..
                          ' actual '..json.encode(stmt.res), {stmt.t}
        end
    end

    return true, 'OK', {}
end;

function refresh_seq_space()
    seq:drop()
    seq = box.schema.space.create('s', {engine='memtx', if_not_exists=true})
    seq:format(g_format)
    local nind = #g_indarr
    for i = 0, nind do
        seq:create_index('IDX'..i, g_indarr[i])
    end
end;

function backup_seq_space()
    local backup = seq:select{}
    return backup
end;

function restore_seq_space(backup)
    refresh_seq_space()
    for _, tup in pairs(backup) do
        seq:insert(tup)
    end
end;

function try_serialize(stmts, ntx, ro_mask, ok_mask, bad_rw_mask)
    local rw_seq = {}
    local ro_pure_set = {}
    local ro_bad_rw_set = {}
    local backup = {}

    local txn_stmts = {}
    for _ = 1, ntx do
        table.insert(txn_stmts, {})
    end

    local nstmts = #stmts
    for i = 1, nstmts do
        local stmt = stmts[i]
        if not ok_mask[stmt.t] then goto continue end

        if stmt.op == 'begin' and ok_mask[stmt.t] and ro_mask[stmt.t] then
            if bad_rw_mask[stmt.t] then
                table.insert(ro_bad_rw_set, stmt.t)
            else
                table.insert(ro_pure_set, stmt.t)
            end
        end
        if stmt.op == 'commit' and ok_mask[stmt.t] and not ro_mask[stmt.t] then
            table.insert(rw_seq, stmt.t)
        end

        if stmt.op ~= 'begin' and stmt.op ~= 'commit' and
            stmt.op ~= 'rollback' then
            table.insert(txn_stmts[stmt.t], stmt)
        end
        ::continue::
    end

    local nro_pure_ok = 0
    local nro_pure = #ro_pure_set
-- Trying to serialize pure ROs first
    for i, s in pairs(ro_pure_set) do
        if try_apply_txn(txn_stmts[s]) then
            nro_pure_ok = nro_pure_ok + 1
            ro_pure_set[i] = nil
        end
    end

-- Trying to serialize ROs with bad RW operations first
    local nro_bad_rw_ok = 0
    local nro_bad_rw = #ro_bad_rw_set
    backup = backup_seq_space()
    for i, s in pairs(ro_bad_rw_set) do
        if try_apply_txn(txn_stmts[s]) then
            nro_bad_rw_ok = nro_bad_rw_ok + 1
            ro_bad_rw_set[i] = nil
        else
            restore_seq_space(backup)
        end
    end
    restore_seq_space(backup)

    local nrw_seq = #rw_seq
    for k = 1, nrw_seq do
        local t = rw_seq[k]
-- Trying to serialize RW
        local result = true
        local what = 'OK'
        local who = {}
        result, what, who = try_apply_txn(txn_stmts[t])
        if result == false then
            return result, what, who
        end
-- Trying to serialize pure ROs after
        for i, s in pairs(ro_pure_set) do
            if try_apply_txn(txn_stmts[s]) then
                nro_pure_ok = nro_pure_ok + 1
                ro_pure_set[i] = nil
            end
        end
-- Trying to serialize ROs with bad RW operations after
        backup = backup_seq_space()
        for i, s in pairs(ro_bad_rw_set) do
            if try_apply_txn(txn_stmts[s]) then
                nro_bad_rw_ok = nro_bad_rw_ok + 1
                ro_bad_rw_set[i] = nil
            else
                restore_seq_space(backup)
            end
        end
        restore_seq_space(backup)
    end

    if nro_pure ~= nro_pure_ok or nro_bad_rw ~= nro_bad_rw_ok then
        local ro_bad = {}
        for _, t in pairs(ro_pure_set) do
            table.insert(ro_bad, t)
        end
        for _, t in pairs(ro_bad_rw_set) do
            table.insert(ro_bad, t)
        end
        return false, 'NOT SERIALIZED RO TXNS '..json.encode(ro_bad), ro_bad
    end

    return true, 0, 'OK', {}
end;

function fill_repro(repro, stmts, ro_mask, ok_mask, bad_rw_mask, bad_txn)
    table.insert(repro, 'txn_proxy = require(\"txn_proxy\")')
    table.insert(repro, 'box.cfg{memtx_use_mvcc_engine = true}')
    table.insert(repro, 's=box.schema.space.create(\"s\", {engine=\"memtx\"})')

    local format = ''
    for i = 1, #g_format do
        local field = g_format[i]
        format = format..string.format(
            '{%q, type=%q, %s}',field[1], field.type,
            field.is_nullable and 'is_nullable=true' or ''
            )
    end
    table.insert(repro, string.format('s:format({%s})', format))

    for i = 0, #g_indarr do
        local index = g_indarr[i]
        local strparts = ''
        for p = 1, #index.parts do
            local part = index.parts[p]
            strparts = strparts..string.format('{%d, %q}, ', part[1], part[2])
        end
        table.insert(repro,
            string.format(
                's:create_index(\"IDX#%d\", {type=%q, parts={%s}, %s})',
                i, index.type, strparts,
                index.unique == false and 'unique=false' or ''
                )
            )
    end

    local skipmask = {}
    for i = 1, #ok_mask do
        table.insert(skipmask, false)
    end

    local nstmts = #stmts
    if bad_txn then
        local is_after = false
        for i = 1, nstmts do
            local stmt = stmts[i]
            if stmt.t == bad_txn and stmt.op == 'commit' then
                is_after = true
            elseif stmt.t ~= bad_txn then
                if (is_after and stmt.op == 'begin') or ro_mask[stmt.t] then
                    skipmask[stmt.t] = true
                end
            end
        end
    end

    for i = 1, nstmts do
        local stmt = stmts[i]
        if skipmask[stmt.t] then goto continue end

        if (not g_is_reproduce_all) and (not ok_mask[stmt.t]) then
-- do nothing
        elseif stmt.op == 'begin' then
            table.insert(repro, 'tx'..stmt.t..' = txn_proxy.new()')
            table.insert(repro, '-- s:dump_history("#'..stmt.t..'BEGIN.dot")')
            table.insert(repro, 'tx'..stmt.t..':begin() -- MUST BE OK')
        elseif stmt.op == 'commit' then
            table.insert(repro, 'tx'..stmt.t..':commit() -- MUST BE OK')
            table.insert(repro, '-- s:dump_history("#'..stmt.t..'COMMIT.dot")')
        elseif g_is_reproduce_all and stmt.op == 'rollback' then
            table.insert(repro, 'tx'..stmt.t..':rollback() -- MUST BE OK')
        elseif stmt.res ~= nil then
            table.insert(repro, stmt.ind and
                'tx'..stmt.t..'(\"s.index['..stmt.ind..']:'..stmt.op..'\")'..
                ' -- RES '..json.encode(stmt.res)
                or
                'tx'..stmt.t..'(\"s:'..stmt.op..'\")'..
                ' -- RES '..json.encode(stmt.res))
        end
        ::continue::
    end
end;

test_run:cmd('setopt delimiter ""');
-- END GUEST API DEFINITIONS
--------------------------------------------------------------------------------

-- START CONNECTION TO HOST
test_run:cmd('set variable listen_port to "mvcc_mass.listen"');
conn = netbox.connect(listen_port)
conn:wait_connected()

result = true
what = 'OK'
displayseed = 0
displayround = 0
bad_txns = {}
repro = {}

seq = box.schema.space.create('s', {engine='memtx', if_not_exists=true})

test_run:cmd('setopt delimiter ";"')
for i = 1, g_nrounds do
    g_indarr[0].type = (math.random(2) == 1) and 'tree' or 'hash'
-- refresh spaces for the next round
    refresh_seq_space()
    conn:call('refresh_sim_space', {g_format, g_indarr})

    stmts, ro_mask, ok_mask, bad_rw_mask, seed =
        conn:call('gen_statements', {g_ntx, g_nro, g_cnt})
    result, what, bad_txns =
        try_serialize(stmts, g_ntx, ro_mask, ok_mask, bad_rw_mask)

    if result == false then
        displayround = i
        displayseed = seed
        local bad = g_is_reproduce_zip and bad_txns[1] or nil
            fill_repro(repro, stmts, ro_mask, ok_mask, bad_rw_mask, bad)
        break
    end
end;
test_run:cmd('setopt delimiter ""');

seq:drop()

-- PRINT RESULT
result, what, displayseed, displayround, bad_txns
-- PRINT REPRODUCER
-- (empty if result is ok, reproduces first failed txn if g_is_reproduce_zip)
repro

conn:close()
-- END CONNECTION TO HOST
--------------------------------------------------------------------------------

-- BEGIN HOST CLEANUP
test_run:cmd('switch mvcc_mass')
box.schema.user.revoke('guest','read,write,execute,create,drop','universe')

sim:drop()

test_run:cmd('switch default')
-- END HOST CLEANUP
--------------------------------------------------------------------------------

-- SHUTDOWN HOST
test_run:cmd('stop server mvcc_mass')
test_run:cmd('cleanup server mvcc_mass')
