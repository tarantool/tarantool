local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

-- Sizes of objects from transaction manager.
-- Please update them, if you changed the relevant structures.
local SIZE_OF_STMT = 136
-- Size of story with one link (for spaces with 1 index).
local SIZE_OF_STORY = 120
-- Size of tuple with 2 number fields
local SIZE_OF_TUPLE = 9
-- Size of xrow for tuples with 2 number fields
local SIZE_OF_XROW = 147
-- Tracker can allocate additional memory, be careful!
local SIZE_OF_READ_TRACKER = 56
local SIZE_OF_CONFLICT_TRACKER = 48
local SIZE_OF_POINT_TRACKER = 88
local SIZE_OF_GAP_TRACKER = 80

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
            if not table_values_are_zeros(v) then
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
    t.assert_equals(server:eval('return box.stat.memtx.tx()'), current_stat)
end

local function tx_step(server, txn_name, op, related_changes)
    server:eval(txn_name ..  '("' .. op .. '")')
    if related_changes then
        table_apply_change(current_stat, related_changes)
    end
    t.assert_equals(server:eval('return box.stat.memtx.tx()'), current_stat)
end

g.before_each(function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()

    g.server:eval('txn_proxy = require("test.box.lua.txn_proxy")')
    g.server:eval('s = box.schema.space.create("test")')
    g.server:eval('s:create_index("pk")')
    -- Clear txm before test
    g.server:eval('box.internal.memtx_tx_gc(100)')
    -- CREATING CURRENT STAT
    current_stat = g.server:eval('return box.stat.memtx.tx()')
    -- Check if txm use no memory
    t.assert(table_values_are_zeros(current_stat))
end)

g.after_each(function()
    -- Check if there is no memory occupied by txm
    g.server:drop()
end)

g.test_simple = function()
    g.server:eval('tx1 = txn_proxy.new()')
    g.server:eval('tx2 = txn_proxy.new()')
    g.server:eval('tx1:begin()')
    g.server:eval('tx2:begin()')
    local diff = {
        ["txn"] = {
            ["statements"] = {
                ["avg"] = math.floor(SIZE_OF_STMT / 2),
                ["total"] = SIZE_OF_STMT,
                ["max"] = SIZE_OF_STMT,
            },
            ["system"] = {
                ["total"] = SIZE_OF_XROW,
                ["max"] = SIZE_OF_XROW,
                ["avg"] = math.floor(SIZE_OF_XROW / 2),
            }
        },
        ["mvcc"] = {
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    }
                }
            }
        }
    }
    tx_step(g.server, 'tx1', "s:replace{1, 1}", diff)
    diff = {
        ["txn"] = {
            ["statements"] = {
                ["avg"] = math.floor(SIZE_OF_STMT / 2),
                ["total"] = SIZE_OF_STMT,
            },
            ["system"] = {
                ["total"] = SIZE_OF_XROW,
                ["avg"] = math.floor(SIZE_OF_XROW / 2) + 1,
            }
        },
        ["mvcc"] = {
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                    ["retained"] = {
                        ["total"] = SIZE_OF_TUPLE,
                        ["count"] = 1,
                    },
                }
            }
        }
    }
    tx_step(g.server, 'tx2', "s:replace{1, 2}", diff)
    diff = {
        ["txn"] = {
            ["statements"] = {
                ["total"] = SIZE_OF_STMT,
                ["avg"] = math.floor(SIZE_OF_STMT / 2),
                ["max"] = SIZE_OF_STMT,
            },
            ["system"] = {
                ["total"] = SIZE_OF_XROW,
                ["avg"] = math.floor(SIZE_OF_XROW / 2),
                ["max"] = SIZE_OF_XROW,
            }
        },
        ["mvcc"] = {
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                }
            }
        }
    }
    tx_step(g.server, 'tx2', "s:replace{2, 2}", diff)
    tx_gc(g.server, 100, nil)
    t.assert_equals(g.server:eval('return tx2:commit()'), '')
    diff = {
        ["txn"] = {
            ["statements"] = {
                ["total"] = -2 * SIZE_OF_STMT,
                ["avg"] = -1 * math.floor(SIZE_OF_STMT / 2),
                ["max"] = -1 * SIZE_OF_STMT,
            },
            ["system"] = {
                ["total"] = -2 * SIZE_OF_XROW,
                ["avg"] = -1 * math.floor(SIZE_OF_XROW / 2),
                ["max"] = -1 * SIZE_OF_XROW,
            }
        },
        ["mvcc"] = {
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = -1 * SIZE_OF_STORY,
                        ["count"] = -1,
                    },
                }
            }
        }
    }
    tx_gc(g.server, 10, diff)
    t.assert_equals(g.server:eval('return tx1:commit()'), '')
    diff = {
        ["txn"] = {
            ["statements"] = {
                ["total"] = -1 * SIZE_OF_STMT,
                ["avg"] = -1 * SIZE_OF_STMT,
                ["max"] = -1 * SIZE_OF_STMT,
            },
            ["system"] = {
                ["total"] = -1 * SIZE_OF_XROW,
                ["avg"] = -1 * SIZE_OF_XROW,
                ["max"] = -1 * SIZE_OF_XROW,
            },
        },
        ["mvcc"] = {
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = -2 * SIZE_OF_STORY,
                        ["count"] = -2,
                    },
                    ["retained"] = {
                        ["total"] = -1 * SIZE_OF_TUPLE,
                        ["count"] = -1,
                    },
                }
            }
        }
    }
    tx_gc(g.server, 100, diff)
    t.assert(table_values_are_zeros(current_stat))
end

g.test_read_view = function()
    g.server:eval('tx1 = txn_proxy.new()')
    g.server:eval('tx2 = txn_proxy.new()')
    g.server:eval('tx1:begin()')
    g.server:eval('tx2:begin()')
    g.server:eval('s:replace{1, 1}')
    g.server:eval('s:replace{2, 1}')
    g.server:eval('box.internal.memtx_tx_gc(10)')
    t.assert(table_values_are_zeros(g.server:eval('return box.stat.memtx.tx()')))
    g.server:eval('tx1("s:get(1)")')
    g.server:eval('tx2("s:replace{1, 2}")')
    g.server:eval('tx2("s:replace{2, 2}")')
    g.server:eval('tx2:commit()')
    local diff = {
        ["mvcc"] = {
            ["trackers"] = {
                ["max"] = SIZE_OF_READ_TRACKER,
                ["avg"] = SIZE_OF_READ_TRACKER,
                ["total"] = SIZE_OF_READ_TRACKER,
            },
            ["tuples"] = {
                ["read_view"] = {
                    ["stories"] = {
                        ["total"] = 3 * SIZE_OF_STORY,
                        ["count"] = 3,
                    },
                    ["retained"] = {
                        ["total"] = SIZE_OF_TUPLE,
                        ["count"] = 1,
                    },
                },
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                    ["retained"] = {
                        ["total"] = SIZE_OF_TUPLE,
                        ["count"] = 1,
                    },
                },
            },
        }
    }
    tx_gc(g.server, 10, diff)
end

g.test_read_view_with_empty_space = function()
    g.server:eval('tx1 = txn_proxy.new()')
    g.server:eval('tx2 = txn_proxy.new()')
    g.server:eval('tx1:begin()')
    g.server:eval('tx2:begin()')
    g.server:eval('s:replace{1, 1}')
    g.server:eval('s:replace{2, 1}')
    g.server:eval('box.internal.memtx_tx_gc(10)')
    t.assert(table_values_are_zeros(g.server:eval('return box.stat.memtx.tx()')))
    g.server:eval('tx1("s:get(1)")')
    g.server:eval('tx2("s:delete(1)")')
    g.server:eval('tx2("s:delete(2)")')
    g.server:eval('tx2:commit()')
    local diff = {
        ["mvcc"] = {
            ["trackers"] = {
                ["max"] = SIZE_OF_READ_TRACKER,
                ["avg"] = SIZE_OF_READ_TRACKER,
                ["total"] = SIZE_OF_READ_TRACKER,
            },
            ["tuples"] = {
                ["read_view"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                    ["retained"] = {
                        ["total"] = SIZE_OF_TUPLE,
                        ["count"] = 1,
                    },
                },
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                    ["retained"] = {
                        ["total"] = SIZE_OF_TUPLE,
                        ["count"] = 1,
                    },
                },
            },
        }
    }
    tx_gc(g.server, 10, diff)
end

g.test_tracker = function()
    g.server:eval('s.index.pk:alter({parts={{field = 1, type = "unsigned"}, {field = 2, type = "unsigned"}}})')
    g.server:eval('s:replace{1, 0}')
    g.server:eval('s:replace{3, 2}')
    g.server:eval('s:replace{2, 0}')
    g.server:eval('tx1 = txn_proxy.new()')
    g.server:eval('tx1:begin()')
    g.server:eval('tx1("s:select{2}")')
    local trackers_used = 2 * SIZE_OF_GAP_TRACKER + SIZE_OF_READ_TRACKER
    local diff = {
        ["mvcc"] = {
            ["trackers"] = {
                ["max"] = trackers_used,
                ["avg"] = trackers_used,
                ["total"] = trackers_used,
            },
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                },
                ["tracking"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                },
            },
        },
    }
    tx_gc(g.server, 10, diff)
end

g.test_conflict = function()
    g.server:eval('tx1 = txn_proxy.new()')
    g.server:eval('tx2 = txn_proxy.new()')
    g.server:eval('box.internal.memtx_tx_gc(10)')
    t.assert(table_values_are_zeros(g.server:eval('return box.stat.memtx.tx()')))
    g.server:eval('tx1:begin()')
    g.server:eval('tx2:begin()')
    g.server:eval('tx1("s:get(1)")')
    g.server:eval('tx2("s:replace{1, 2}")')
    g.server:eval("box.internal.memtx_tx_gc(10)")
    local trackers_used = SIZE_OF_CONFLICT_TRACKER + SIZE_OF_POINT_TRACKER
    local diff = {
        ["txn"] = {
            ["statements"] = {
                ["max"] = SIZE_OF_STMT,
                ["avg"] = math.floor(SIZE_OF_STMT / 2),
                ["total"] = SIZE_OF_STMT,
            },
            ["system"] = {
                ["max"] = SIZE_OF_XROW,
                ["avg"] = math.floor(SIZE_OF_XROW / 2),
                ["total"] = SIZE_OF_XROW,
            },
        },
        ["mvcc"] = {
            ["trackers"] = {
                ["max"] = trackers_used,
                ["avg"] = math.floor(trackers_used / 2),
                ["total"] = trackers_used,
            },
            ["conflicts"] = {
                ["max"] = 0,
                ["avg"] = 0,
                ["total"] = 0,
            },
            ["tuples"] = {
                ["used"] = {
                    ["stories"] = {
                        ["total"] = SIZE_OF_STORY,
                        ["count"] = 1,
                    },
                },
            },
        },
    }
    tx_gc(g.server, 10, diff)
end

g.test_user_data = function()
    g.server:eval('ffi = require("ffi")')
    g.server:eval('ffi.cdef("void *box_txn_alloc(size_t size);")')
    g.server:eval('tx = txn_proxy.new()')
    g.server:eval('tx:begin()')
    local alloc_size = 100
    local diff = {
        ["txn"] = {
            ["user"] = {
                ["total"] = alloc_size,
                ["avg"] = alloc_size,
                ["max"] = alloc_size,
            },
        },
    }
    tx_step(g.server, 'tx', 'ffi.C.box_txn_alloc(' .. alloc_size .. ')', diff)
    t.assert_equals(g.server:eval('return tx:commit()'), '')
    diff = {
        ["txn"] = {
            ["user"] = {
                ["total"] = -1 * alloc_size,
                ["avg"] = -1 * alloc_size,
                ["max"] = -1 * alloc_size,
            },
        },
    }
    tx_gc(g.server, 1, diff)
end

g.test_gh_8448_box_stat_memtx_func = function()
    g.server:exec(function()
        t.assert_type(box.stat.memtx.tx, 'function')
        t.assert_equals(box.stat.memtx().tx, box.stat.memtx.tx())
    end)
end
