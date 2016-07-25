--
-- hermitage: Testing transaction isolation levels.
-- github.com/ept/hermitage
--
-- Testing Vinyl transactional isolation in Tarantool.
-- 
-- *************************************************************************
-- 1.7 setup begins
-- *************************************************************************
net = require('net.box')
address  = os.getenv('ADMIN')
box.schema.user.grant('guest', 'read,write,execute', 'universe')
yaml = require('yaml')
test_run = require('test_run').new()

_ = box.schema.space.create('test', {engine = 'vinyl'})
_ = box.space.test:create_index('pk')

c1 = net:new(address)
c2 = net.new(address)
c3 = net.new(address)


test_run:cmd("setopt delimiter ';'")
getmetatable(c1).__call = function(c, command)
    local f = yaml.decode(c:console(command))
    if type(f) == 'table' then
        setmetatable(f, {__serialize='array'})
    end
    return f
end;
test_run:cmd("setopt delimiter ''");
methods = getmetatable(c1)['__index']
methods.begin = function(c) return c("box.begin()") end
methods.commit = function(c) return c("box.commit()") end
methods.rollback = function(c) return c("box.rollback()") end
t = box.space.test
-- *************************************************************************
-- 1.7 setup up marker: end of test setup
-- *************************************************************************

-- ------------------------------------------------------------------------
-- READ COMMITTED basic requirements: G0
-- ------------------------------------------------------------------------
--
-- REPLACE
--
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:replace{1, 11}")
c2("t:replace{1, 12}")
c1("t:replace{2, 21}")
c1:commit()
c2("t:replace{2, 22}")
c2:commit() -- rollback
t:get{1} -- {1, 11}
t:get{2} -- {2, 21}

-- teardown
t:delete{1}
t:delete{2}
--
-- UPDATE
--
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:update(1, {{'=', 2, 11}})")
c2("t:update(1, {{'=', 2, 12}})")
c1("t:update(2, {{'=', 2, 21}})")
c1:commit()
c2("t:update(2, {{'=', 2, 22}})")
c2:commit() -- rollback
t:get{1} -- {1, 11}
t:get{2} -- {2, 21}

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- READ COMMITTED basic requirements: G1A
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:replace{1, 101}")
c2("t:replace{1, 10}")
c1:rollback()
c2("t:get{1}") -- {1, 10}
c2:commit() -- true

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- READ COMMITTED basic requirements: G1B
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:replace{1, 101}")
c2("t:get{1}") -- {1, 10}
c1("t:replace{1, 11}")
c1:commit() -- ok
c2("t:get{1}") -- {1, 10}
c2:commit() -- ok

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- Circular information flow: G1C
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:replace{1, 11}")
c2("t:replace{2, 22}")
c1("t:get{2}") -- {2, 20}
c2("t:get{1}") -- {1, 10}
c1:commit() -- ok
c2:commit() -- ok

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- OTV: observable transaction vanishes
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c3:begin()
c1("t:replace{1, 11}")
c1("t:replace{2, 19}")
c2("t:replace{1, 12}")
c1:commit() -- ok
c3("t:get{1}") -- {1, 11}
c2("t:replace{2, 18}")
c3("t:get{2}") -- {2, 19}
c2:commit() -- rollback -- conflict
c3("t:get{2}") -- {2, 19}
c3("t:get{1}") -- {1, 11}
c3:commit()

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
--  PMP: Predicate with many preceders
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:get{3}")
c2("t:replace{3, 30}")
c2:commit() -- ok
c1("t:get{1}") -- {1, 10}
c1("t:get{2}") -- {2, 20}
c1("t:get{3}") -- nothing
c1:commit() -- ok

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
--  PMP write: predicate many preceders for write predicates
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:replace{1, 20}")
c1("t:replace{2, 30}")
c2("t:get{1}") -- {1, 10}
c2("t:get{2}") -- {2, 20}
c2("t:delete{2}")
c1:commit() -- ok
c2("t:get{1}") -- {1, 10}
c2:commit() -- rollback -- conflict

t:get{1} -- {1, 20}
t:get{2} -- {2, 30}

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
--  P4: lost update: don't allow a subsequent commit to lose update
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:get{1}") -- {1, 10}
c2("t:get{1}") -- {1, 10}
c1("t:replace{1, 11}")
c2("t:replace{1, 12}")
c1:commit() -- ok
c2:commit() -- rollback -- conflict

-- teardown
t:delete{1}
t:delete{2}
------------------------------------------------------------------------
-- G-single: read skew
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:get{1}") -- {1, 10}
c2("t:get{1}") -- {1, 10}
c2("t:get{2}") -- {2, 20}
c2("t:replace{1, 12}")
c2("t:replace{2, 18}")
c2:commit() -- ok
c1("t:get{2}") -- {2, 20}
c1:commit() -- ok

-- teardown
t:delete{1}
t:delete{2}
------------------------------------------------------------------------
-- G-single: read skew, test with write predicate
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:get{1}") -- {1, 10}
c2("t:get{1}") -- {1, 10}
c2("t:get{2}") -- {2, 20}
c2("t:replace{1, 12}")
c2("t:replace{2, 18}")
c2:commit() -- T2
c1("t:delete{2}")
c1("t:get{2}") -- finds nothing
c1:commit() -- rollback

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- G2-item: write skew
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
c1("t:get{1}") -- {1, 10}
c1("t:get{2}") -- {2, 20}
c2("t:get{1}") -- {1, 10}
c2("t:get{2}") -- {2, 20}
c1("t:replace{1, 11}")
c2("t:replace{1, 21}")
c1:commit() -- ok
c2:commit() -- rollback -- conflict

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- G2: anti-dependency cycles
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c2:begin()
-- select * from test where value % 3 = 0
c1("t:get{1}") -- {1, 10}
c1("t:get{2}") -- {2, 20}
c2("t:get{1}") -- {1, 10}
c2("t:get{2}") -- {2, 20}
c1("t:replace{3, 30}")
c2("t:replace{4, 42}")
c1:commit() -- ok
c2:commit() -- rollback

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
-- G2: anti-dependency cycles with two items
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c1("t:get{1}") -- {1, 10}
c1("t:get{2}") -- {2, 20}
c2:begin()
c2("t:replace{2, 25}")
c2:commit() -- ok
c3:begin()
c3("t:get{1}") -- {1, 10}
c3("t:get{2}") -- {2, 25}
c3:commit() -- ok
c1("t:replace{1, 0}")
c1:commit() -- rollback

-- teardown
t:delete{1}
t:delete{2}
-- ------------------------------------------------------------------------
--  G2: anti-dependency cycles with two items (no replace)
-- ------------------------------------------------------------------------
-- setup
t:replace{1, 10}
t:replace{2, 20}

c1:begin()
c1("t:get{1}") -- {1, 10}
c1("t:get{2}") -- {2, 20}
c2:begin()
c2("t:replace{2, 25}")
c2:commit() -- ok
c3:begin()
c3("t:get{1}") -- {1, 10}
c3("t:get{2}") -- {2, 25}
c3:commit() -- ok
-- c1("t:replace{1, 0)")
c1:commit() -- ok

-- teardown
t:delete{1}
t:delete{2}
-- *************************************************************************
-- 1.7 cleanup marker: end of test cleanup
-- *************************************************************************
--
box.space.test:drop()
c1 = nil
c2 = nil
c3 = nil
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
