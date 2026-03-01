local server = require('luatest.server')
local t = require('luatest')

local g = t.group("autoinc", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_autoincrement = function(cg)
    cg.server:exec(function(engine)
        -- Not automatic sequence should not be removed by DROP TABLE.
        box.schema.user.create('user1')
        local test_space = box.schema.create_space('t', {
            engine = engine,
            format = {{'i', 'integer'}},
        })
        local seq = box.schema.sequence.create('S')
        test_space:create_index('I', {sequence = 'S'})
        box.schema.user.grant('user1', 'write', 'sequence', 'S')
        box.execute('DROP TABLE t;')
        local seqs = box.space._sequence:select{}
        t.assert_equals(#seqs == 1 and seqs[1].name == seq.name or seqs, true)
        seq:drop()

        -- Automatic sequence should be removed at DROP TABLE, together
        -- with all the grants.
        box.execute('CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT);')
        seqs = box.space._sequence:select{}
        t.assert_equals(#seqs == 1 or seqs, true)
        seq = seqs[1].name
        box.schema.user.grant('user1', 'write', 'sequence', seq)
        box.execute('DROP TABLE t;')
        seqs = box.space._sequence:select{}
        t.assert_equals(#seqs == 0 or seqs, true)

        box.schema.user.drop('user1')
    end)
end
