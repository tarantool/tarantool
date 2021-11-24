local t = require('luatest')
local Cluster =  require('test.luatest_helpers.cluster')

local pg = t.group('txm')

local current_stat = {}

local function table_apply_change(table, related_changes)
    for k, v in pairs(related_changes) do
        if type(v) ~= 'table' then
            table[k] = table[k] + v
        else
            table_apply_change(table[k], v)
        end
    end
end

local function table_values_are_zeros(table)
    for _, v in pairs(table) do
        if type(v) ~= 'table' then
            if v ~= 0 then
                return false
            end
        else
            if table_values_are_zeros(v) == false then
                return false
            end
        end
    end
    return true
end

local function tx_gc(server, steps, related_changes)
    server:eval('box.internal.memtx_tx_gc(' .. steps .. ')')
    if related_changes then
        table_apply_change(current_stat, related_changes)
    end
    assert(table.equals(current_stat, server:eval('return box.stat.memtx.mvcc()')))
end

local function tx_step(server, txn_name, op, related_changes)
    server:eval(txn_name ..  '("' .. op .. '")')
    if related_changes then
        table_apply_change(current_stat, related_changes)
    end
    assert(table.equals(current_stat, server:eval('return box.stat.memtx.mvcc()')))
end

pg.before_each(function(cg)
    cg.cluster = Cluster:new({})
    local box_cfg = {
        memtx_use_mvcc_engine = true
    }
    cg.txm_server = cg.cluster:build_server({alias = 'txm_server', box_cfg = box_cfg})
    cg.cluster:add_server(cg.txm_server)
    cg.cluster:start()

    cg.txm_server:eval('txn_proxy = require("test.box-luatest.lua.txn_proxy")')
    cg.txm_server:eval('s = box.schema.space.create("test")')
    cg.txm_server:eval('s:create_index("pk")')
    cg.txm_server:eval('box.internal.memtx_tx_gc(100)')
    -- CREATING CURRENT STAT
    current_stat = cg.txm_server:eval('return box.stat.memtx.mvcc()')
    -- Check if txm use no memory
    assert(table_values_are_zeros(current_stat))
end)

pg.after_each(function(cg)
    -- Check if there is no memory occupied by txm
    assert(table_values_are_zeros(current_stat))
    cg.cluster.servers = nil
    cg.cluster:drop()
end)

pg.test_txm_mem = function(cg)
    cg.txm_server:eval('tx1 = txn_proxy.new()')
    cg.txm_server:eval('tx2 = txn_proxy.new()')
    cg.txm_server:eval('tx1:begin()')
    cg.txm_server:eval('tx2:begin()')
    local diff = {
        ["STATEMENTS"] = {
            ["avg"] = 60,
            ["total"] = 120,
            ["max"] = 128
        },
        ["STORIES"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = 152
            }
        },
        ["REDO LOGS"] = {
            ["total"] = 147,
            ["max"] = 512,
            ["avg"] = 73
        }
    }
    tx_step(cg.txm_server, 'tx1', "s:replace{1, 1}", diff)
    diff = {
        ["STATEMENTS"] = {
            ["total"] = 120,
            ["avg"] = 60
        },
        ["STORIES"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = 152
            }
        },
        ["TUPLE PINNED"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = 9,
                ["count"] = 1
            }
        },
        ["REDO LOGS"] = {
            ["total"] = 147,
            ["avg"] = 74
        }
    }
    tx_step(cg.txm_server, 'tx2', "s:replace{1, 2}", diff)
    diff = {
        ["STATEMENTS"] = {
            ["total"] = 120,
            ["avg"] = 60,
            ["max"] = 512 - 128
        },
        ["STORIES"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = 152
            }
        },
        ["REDO LOGS"] = {
            ["total"] = 147,
            ["avg"] = 73
        }
    }
    tx_step(cg.txm_server, 'tx2', "s:replace{2, 2}", diff)
    tx_gc(cg.txm_server, 100, nil)
    local err = cg.txm_server:eval('return tx2:commit()')
    assert(not err[1])
    diff = {
        ["STATEMENTS"] = {
            ["total"] = -120 * 2,
            ["avg"] = -60,
            ["max"] = 128 - 512
        },
        ["STORIES"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = -152
            }
        },
        ["REDO LOGS"] = {
            ["total"] = -147 * 2,
            ["avg"] = -73
        }
    }
    tx_gc(cg.txm_server, 10, diff)
    err = cg.txm_server:eval('return tx1:commit()')
    assert(not err[1])
    diff = {
        ["STATEMENTS"] = {
            ["total"] = -120,
            ["avg"] = -120,
            ["max"] = -128
        },
        ["STORIES"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = -152 * 2
            }
        },
        ["REDO LOGS"] = {
            ["total"] = -147,
            ["avg"] = -147,
            ["max"] = -512
        },
        ["TUPLE PINNED"] = {
            ["USED BY ACTIVE TXNS"] = {
                ["total"] = -9,
                ["count"] = -1
            }
        }
    }
    tx_gc(cg.txm_server, 100, diff)
end
