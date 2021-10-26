test_run = require('test_run').new()
-- need to restart in order to reset box.stat.vinyl() stats
test_run:cmd("restart server default")

txn_proxy = require('txn_proxy')

_ = box.schema.space.create('test', {engine = 'vinyl'})
_ = box.space.test:create_index('pk')

c1 = txn_proxy.new()
c2 = txn_proxy.new()
c3 = txn_proxy.new()
c4 = txn_proxy.new()
c5 = txn_proxy.new()
c6 = txn_proxy.new()
c7 = txn_proxy.new()


t = box.space.test

--
-- empty transaction commit
--
c1:begin()
c1:commit()

--
-- empty transaction rollback
--
c1:begin()
c1:rollback()

--
-- single-statement transaction commit
--
c1:begin()
c1("t:replace{1}")
c1:commit()
c1("t:get{1}")

-- cleanup
c1("t:delete{1}")

--
-- single-statement transaction rollback
--
c1:begin()
c1("t:replace{1}")
c1:rollback()
c1("t:get{1}")

--
-- basic effects: if a transaction is rolled back, it has no effect
--
c1:begin()
c1("t:insert{1}")
c1("t:get{1}")
c1:rollback()
c1("t:get{1}")
c2("t:get{1}")

--
-- multi-statement transaction
--
test_run:cmd("setopt delimiter ';'")
c1:begin();
for i = 1,100 do
    c1(string.format("t:insert{%d}", i))
    assert(c1(string.format("t:get{%d}", i))[1][1] == i)
end;
c1:commit();
for i = 1,100 do
    c1(string.format("t:delete{%d}", i))
end;
for i = 1,100 do
    assert(#c1(string.format("t:get{%d}", i)) == 0)
end;
test_run:cmd("setopt delimiter ''");

--
-- multi-statement transaction rollback
--
test_run:cmd("setopt delimiter ';'")
c1:begin();
for i = 1,100 do
    c1(string.format("t:insert{%d}", i))
    assert(c1(string.format("t:get{%d}", i))[1][1] == i)
end;
c1:rollback();
for i = 1,100 do
    assert(#c1(string.format("t:get{%d}", i)) == 0)
end;
test_run:cmd("setopt delimiter ''");

-- transaction_set_set_get_commit(void)

c1:begin()

c1("t:replace{1, 1}")
c1("t:replace{1, 2}")
c1("t:get{1}")
c1:commit()
c1("t:get{1}")
c1("t:delete{1}")

-- transaction_set_set_commit_get(void)

c1:begin()
c1("t:replace{1}")
c1("t:replace{1, 2}")
c1:commit()

c2:begin()
c2("t:get{1}")
c2:rollback()

c1("t:delete{1}")

-- transaction_set_set_rollback_get(void)

c1:begin()

c1("t:replace{1}")
c1("t:replace{1, 2}")
c1:rollback()

c2:begin()
c2("t:get{1}")
c2:rollback()

-- transaction_set_delete_get_commit(void)

c1:begin()
c1("t:insert{1}")
c1("t:delete{1}")
c1("t:get{1}")
c1:commit()

-- transaction_set_delete_get_commit_get(void)

c1:begin()

c1("t:insert{1}")
c1("t:delete{1}")
c1("t:get{1}")
c1:commit()

c1("t:get{1}")

--
-- transaction_set_delete_set_commit_get(void)
--
c1:begin()

c1("t:insert{1, 1}")
c1("t:delete{1}")
c1("t:insert{1, 2}")
c1("t:get{1}")
c1:commit()
c2("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")

--
-- transaction_set_delete_commit_get_set(void)
--
c1:begin()

c1("t:insert{1}")
c1("t:delete{1}")
c1:commit()

c1("t:get{1}")
c1("t:insert{1}")
c1("t:get{1}")
c1("t:delete{1}")
c1("t:get{1}")

--
-- transaction_p_set_commit(void)
--
c1:begin()
c2:begin()

c1("t:replace{1, 10}")
c1:commit()

c2("t:replace{2, 15}");
c2:commit()

c1("t:get{1}")
c1("t:get{2}")
c1("t:delete{1}")
c1("t:delete{2}")

--
-- no dirty reads: if a transaction is not committed, its effects are not
-- visible
--

c1:begin()
c1("t:insert{1}")
c1("t:get{1}")
--
-- not visible in c2
--
c2("t:get{1}")
c1:commit()
--
-- become visible in c2 after c1 commits (c2 runs in autocommit)
--
c2("t:get{1}")

--
-- repeatable read: if c1 has read X, and there was
-- another transaction, which modified X after c1 started,
-- and c1 reads X again, it gets the same result
--
c1:begin()
c1("t:get{1}")
--
-- not visible in c1
--
c2("t:replace{1, 'c2'}")

c1("t:get{1}")
c2:commit()
--
-- still not visible, even though c2 has committed
--
c1("t:get{1}")
-- commits ok since is a read only transaction
c1:commit()
--
-- now visible
--
c1("t:get{1}")
c1("t:delete{1}")

-- *******************************
-- tx manager tests  from sophia *
-- *******************************
-- --------------------------------------------------------------------------
-- transaction_p_set_get_commit(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine

c1("t:replace{1, 10}")
c1("t:get{1}") -- {1, 10}
--
c1:commit()
--
--
c2("t:replace{2, 15}")
--
c2("t:get{2}") -- {2, 15}
--
c2:commit()
--
-- cleanup
--
c1("t:delete{1}")
c1("t:delete{2}")
--
-- --------------------------------------------------------------------------
-- transaction_p_set_commit_get0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c1:commit()
--
c2("t:replace{2, 15}")
c2:commit()
--
c1:begin()
c1("t:get{1}") -- {1, 10}
--
c1("t:get{2}") -- {2, 15}
c1:rollback()
--
-- cleanup
--
c1("t:delete{1}")
c1("t:delete{2}")

-- --------------------------------------------------------------------------
-- transaction_p_set_commit_get1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}")
c2("t:get{200}")
--
c2("t:replace{1, 10}")
c2:commit()
--
-- try writing an unrelated key
--
c1("t:replace{2, 15}")
c1:commit()
--
c2:begin()
c2("t:get{1}") -- {1, 10}
c2:rollback()
--
-- cleanup
--
c1("t:delete{1}")
c1("t:delete{2}")
-- --
--  now try the same key
-- --
c1:begin()
c2:begin()
c1("t:get{100}")
c2("t:get{200}")
--
c2("t:replace{1, 10}")
c2:commit()
--
c1("t:replace{1, 15}")
c1:commit()
--
c2:begin()
c2("t:get{1}") -- {1, 15}
c2:rollback()
--
-- cleanup
--
c1("t:delete{1}")
-- --------------------------------------------------------------------------
-- transaction_p_set_commit_get2(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}")
c2("t:get{200}")
--
--
c1("t:replace{2, 15}")
c1:commit()
--
--
c2("t:replace{1, 10}")
c2:commit() -- commits successfully
--
c1:begin()
c1("t:get{1}") -- {1, 10}
--
c1("t:get{2}") -- {2, 15}
c1:rollback()
--
-- cleanup
--
c1("t:delete{1}")
c1("t:delete{2}")

-- --------------------------------------------------------------------------
-- transaction_p_set_rollback_get0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}")
c2("t:get{200}")
--
--
c1("t:replace{1, 10}")
c1:rollback()
--
c2("t:replace{2, 15}")
c2:rollback()
--
c3:begin()
c3("t:get{1}") -- finds nothing
c3("t:get{2}") -- finds nothing
c3:rollback()

-- --------------------------------------------------------------------------
-- transaction_p_set_rollback_get1(void)
-- --------------------------------------------------------------------------
--
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 10}")
c2:rollback()
--
c1("t:replace{2, 15}")
c1:rollback()
--
c3:begin()
c3("t:get{1}") -- finds nothing
c3("t:get{2}") -- finds nothing
c3:rollback()
--
-- --------------------------------------------------------------------------
-- transaction_p_set_rollback_get2(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
--
c2("t:replace{1, 10}")
c2:rollback()
--
c1("t:replace{1, 15}")
c1:rollback()
--
c3("t:get{1}") -- finds nothing
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
--
c2("t:replace{1, 10}")
c2:rollback()
--
c1("t:replace{1, 15}")
c1:commit()
--
c3("t:get{1}") -- {1, 15}
--
-- cleanup
--
c3("t:delete{1}")
-- --------------------------------------------------------------------------
-- transaction_c_set_commit0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c1("t:replace{1, 10}")
c1:commit()
--
c2("t:replace{1, 15}")
c2:commit()
--
c2("t:get{1}")  -- {1,15}
-- cleanup
--
c1("t:delete{1}")

-- --------------------------------------------------------------------------
-- transaction_c_set_commit1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 10}")
c2:commit()
--
c1("t:replace{1, 15}")
c1:commit()
--
c3("t:get{1}") -- {1, 15}
--
-- cleanup
--
c3("t:delete{1}")

-- --------------------------------------------------------------------------
-- transaction_c_set_commit2(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 15}")
--
c2("t:replace{1, 10}")
--
c2:commit()
c1:commit()
--
c3("t:get{1}") -- {1, 15}
--
-- cleanup
--
c1("t:delete{1}")
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 15}")
--
c2("t:replace{1, 10}")
--
-- sic: commit order
c1:commit()
c2:commit() -- write after write is ok, the last writer to commit wins
--
c3("t:get{1}") -- {1, 10}
--
-- cleanup
--
c1("t:delete{1}")
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_rollback_a0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 10}")
--
c2:rollback()
--
c1("t:replace{1, 15}")
--
c1:commit()
--
c3("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")
--
--
-- statement order is irrelevant, rollback order is important
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
c2("t:replace{1, 15}")
--
c2:rollback()
c1:commit()
--
c3("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_rollback_a1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 10}")
c1("t:replace{1, 15}")
--
c2:rollback()
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- statements in different order now
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
c2("t:replace{1, 15}")
--
c2:rollback()
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_rollback_b0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 10}")
c2:commit() -- success
--
c1("t:replace{1, 15}")
c1:rollback() -- success
--
c3("t:get{1}")
-- cleanup
--
c1("t:delete{1}")

-- --------------------------------------------------------------------------
-- transaction_c_set_commit_rollback_b1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 15}")
c1("t:replace{1, 10}")
--
c2:commit()
c1:rollback()
--
c3("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")
--
-- now commit the second transaction
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 15}")
c1("t:replace{1, 10}")
--
c2:commit()
c1:commit() -- ok, the last committer wins
--
c3("t:get{1}") -- {1, 10}
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_rollback_ab0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 15}")
c2:rollback()
--
c1("t:replace{1, 10}")
c1:rollback()
--
c3("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")
--
-- now commit the second transaction
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 15}")
c2:rollback()
--
c1("t:replace{1, 10}")
c1:commit()
--
c3("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_rollback_ab1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 10}")
c1("t:replace{1, 15}")
--
c2:rollback()
c1:rollback()
--
c3("t:get{1}")
--
-- cleanup
--
c2("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_a0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c2("t:replace{1, 15}")
--
c1("t:replace{1, 10}")
--
c1:commit() -- success
c2:commit() -- success, the last writer wins
--
c2("t:get{1}") -- {1, 15}
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_a1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c1:commit() -- success, the last writer wins
--
-- cleanup
--
c2("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_b0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_b1(void)
-- --------------------------------------------------------------------------
--
c2:begin()
c1:begin()
c2("t:get{100}") -- start transaction in the engine
c1("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_a0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_a1(void)
-- --------------------------------------------------------------------------
--
c2:begin()
c1:begin()
c2("t:get{100}") -- start transaction in the engine
c1("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c1:rollback() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_b0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c2:rollback() -- not in transaction
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_b1(void)
-- --------------------------------------------------------------------------
--
c2:begin()
c1:begin()
c2("t:get{100}") -- start transaction in the engine
c1("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c2:commit() -- success
c2:rollback() -- not in transaction
c1:commit() -- success
--
-- cleanup
--
c1("t:delete{1}")
c1("t:delete{2}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_n0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c3:begin()
c3("t:get{300}") -- start transaction in the engine
--
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c3("t:replace{1, 20}")
--
c2:commit() -- success
c3:commit() -- success
c1:commit() -- success, the last committer wins
c2:commit() -- not in transaction
c3:commit() -- not in transaction
--
c3:get{1} -- {1, 20}
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_n1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c3:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c3("t:get{200}") -- start transaction in the engine
c2("t:get{300}") -- start transaction in the engine
--
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 20}")
--
c3("t:replace{1, 30}")
--
c1:commit() -- success
c2:commit() -- success
c3:commit() -- success
--
c3("t:get{1}") -- {1, 30}
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_n0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c3:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c3("t:get{300}") -- start transaction in the engine
--
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c3("t:replace{1, 20}")
--
c2:commit() -- success
c3:commit() -- rollback
c1:rollback() -- success
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_n1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c3:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c3("t:get{300}") -- start transaction in the engine
--
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c3("t:replace{1, 20}")
--
c2:commit()  -- success
c3:commit() -- rollback
c2:rollback() -- success, not in transaction in tarantool
c3:commit() -- success, not in transaction in tarantool
c1:commit() -- rollback
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_n2(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c3:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c3("t:get{300}") -- start transaction in the engine
--
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c3("t:replace{1, 20}")
--
c3:rollback()
c2:commit()
c1:commit()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_n3(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c3:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c3("t:get{300}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c3("t:replace{1, 20}")
--
c2:commit()
c3:rollback()
c1:commit()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_commit_wait_rollback_n4(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c3:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
c3("t:get{300}") -- start transaction in the engine
--
--
c1("t:replace{1, 10}")
--
c2("t:replace{1, 15}")
--
c3("t:replace{1, 20}")
--
c2:commit()
c3:rollback()
c1:rollback()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_get0(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
c1:commit()
--
c2("t:get{1}") -- find newest {1, 10}
--
c2("t:replace{1, 15}")
c2:commit() -- rollback
--
c3:begin()
c3("t:get{1}") -- {1, 10}
c3:commit()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_get1(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine
--
c1("t:replace{1, 10}")
--
c1:rollback()
--
c2("t:get{1}") -- finds nothing
--
c2("t:replace{1, 15}")
c2:commit()
--
c3:begin()
c3("t:get{1}") -- {1, 15}
c3:commit()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_get2(void)
-- --------------------------------------------------------------------------
--
c7:begin()
c7("t:get{100}") -- start transaction in the engine
c1:begin()
--
c1("t:replace{1, 1}")
--
c2:begin()
--
c2("t:replace{1, 2}")
--
c4:begin()
c4("t:replace{1, 4}")
--
c5:begin()
c5("t:replace{1, 5}")
--
c6:begin()
c6("t:get{100}") -- start transaction in the engine
--
c1("t:get{1}") -- {1, 1}
--
c2("t:get{1}") --  {1, 2}
--
c4("t:get{1}") -- {1, 4}
--
c5("t:get{1}") -- {1, 5}
--
c6("t:get{1}") --  nothing
--
c7("t:get{1}") --  nothing
--
c3:begin()
--
c3("t:get{1}") -- nothing
c3:rollback()
--
c1:rollback()
c2:rollback()
c3:rollback()
c4:rollback()
c5:rollback()
c6:rollback()
c7:rollback()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_get3(void)
-- --------------------------------------------------------------------------
--
c7:begin()
c1:begin()
c7("t:get{100}") -- start transaction in the engine
c1("t:get{1}") -- start transaction in the engine
--
c3:begin()
c3("t:replace{1, 3}")
c3:commit()
--
c2:begin()
c3:begin()
c2("t:get{500}") -- start transaction in the engine
c3("t:get{600}") -- start transaction in the engine
c2("t:get{1}") -- {1, 3}
--
c3("t:replace{1, 6}")
c3:commit() -- c2 goes to read view now
--
c4:begin()
c3:begin()
--
c3("t:replace{1, 9}")
c3:commit()
--
c5:begin()
c3:begin()
c5("t:get{800}") -- start transaction in the engine
c3("t:get{900}") -- start transaction in the engine
--
c3("t:replace{1, 12}")
c3:commit()
--
c6:begin()
c6("t:get{1000}") -- start transaction in the engine
--
c2("t:get{1}") -- {1, 3}
--
c4("t:get{1}") -- {1, 12}
--
c5("t:get{1}") -- {1, 12}
--
c6("t:get{1}") -- {1, 12}
--
c3:begin()
c3("t:get{1}") -- {1, 12}
c3:rollback()
--
c1("t:get{1}") -- nothing
--
c7("t:get{1}") -- {1, 12}
--
c2:rollback()
--
c4("t:get{1}") -- {1, 12}
--
c5("t:get{1}") -- {1, 12}
--
c6("t:get{1}") -- {1, 12}
--
c3:begin()
c3("t:get{1}") -- {1, 12}
c3:rollback()
--
c1("t:get{1}") -- nothing
--
c7("t:get{1}") -- {1, 12}
--
c4:rollback()
--
c5("t:get{1}") -- {1, 12}
--
c6("t:get{1}") -- {1, 12}
--
c3:begin()
c3("t:get{1}") -- {1, 12}
c3:rollback()
--
c1("t:get{1}") -- nothing
--
c7("t:get{1}") -- {1, 12}
--
c5:rollback()
--
c6("t:get{1}") -- {1, 12}
--
c3:begin()
c3("t:get{1}") -- {1, 12}
c3:rollback()
--
c1("t:get{1}") -- nothing
--
c7("t:get{1}") -- {1, 12}
--
c6:rollback()
--
c3:begin()
c3("t:get{1}") -- {1, 12}
c3:rollback()
--
c1("t:get{1}") -- nothing
--
c7("t:get{1}") -- {1, 12}
--
c1:rollback()
c7:rollback()
--
c3:begin()
c3("t:get{1}") -- {1, 12}
c3:rollback()
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_c_set_conflict_derive(void)
-- --------------------------------------------------------------------------
--
c1:begin()
c2:begin()
c1("t:get{100}") -- start transaction in the engine
c2("t:get{200}") -- start transaction in the engine

c1("t:replace{1, 10}")
c2("t:replace{1, 15}")
--
c1:commit()
--
c2("t:replace{1, 20}") -- should not reset conflict flag
--
c2:commit() --  rollback
--
c3("t:get{1}")
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- transaction_sc_set_wait(void)
-- --------------------------------------------------------------------------
--
c1:begin()
--
c1("t:replace{1, 10}")
--
-- sic: runs in autocommit mode
--
c2("t:replace{1, 15}")
--
c1:commit()
--
c2("t:get{1}") -- {1, 10}
--
c1("t:delete{1}")

-- --------------------------------------------------------------------------
-- transaction_sc_get(void)
-- --------------------------------------------------------------------------
--
c1("t:replace{1, 7}")
--
c2:begin()
--
c2("t:replace{1, 8}")
--
c1("t:get{1}") -- {1, 7}
--
c2:commit()
--
c1("t:get{1}") -- {1, 8}
--
c3("t:get{1}") -- {1, 8}
--
-- cleanup
--
c1("t:delete{1}")
-- --------------------------------------------------------------------------
-- two conflicting inserts
-- --------------------------------------------------------------------------
c1:begin()
c2:begin()
--
c1("t:insert{1, 10}")
--
c2("t:insert{1, 15}")
--
c1:commit() -- success
c2:commit() -- rollback, c2 reads {1} before writing it
--
c3("t:get{1}") -- {1, 10}
--
--
-- cleanup
--
c1("t:delete{1}")
--
c1:begin()
c2:begin()
--
c1("t:insert{1, 10}")
--
c2("t:insert{1, 15}")
--
c2:commit() -- success
c1:commit() -- rollback, c1 reads {1} before writing it
--
c3("t:get{1}") -- {1, 15}
--
--
-- cleanup
--
c1("t:delete{1}")
--
-- --------------------------------------------------------------------------
-- Transaction spuriously abort based on CSN clock
-- --------------------------------------------------------------------------
t:insert{1, 10}
t:insert{2, 20}

c7:begin()
c7("t:insert{8, 800}")

c3:begin()
c3("t:get{1}")
c3:commit()

c1:begin()
c2:begin()
--
c1("t:replace{4, 40}")
--
c2("t:get{1}")
--
c3:begin()
c3("t:insert{3, 30}")
c3:commit()
--
c2("t:replace{5, 50}")
c1("t:get{1}")

c1:commit()
c2:commit()
c7:rollback()
--
-- cleanup
--
t:delete{1}
t:delete{2}
t:delete{3}
t:delete{4}
t:delete{5}
-- --------------------------------------------------------------------------
-- Conflict manager works for iterators
-- --------------------------------------------------------------------------
t:insert{1, 10}
t:insert{2, 20}

c1:begin()
c2:begin()
c1("t:select{}")
c2("t:select{}")
c1("t:replace{1, 'new'}")
c2("t:replace{2, 'new'}")
c1:commit()
c2:commit() -- rollback
--
--
-- gh-1606 visibility of changes in transaction in range queries
--
c1:begin()
c1("t:select{}")
c1("t:replace{3, 30}")
c1("t:select{}")
c1("t:select({3}, {iterator='ge'})")
c1("t:select({3}, {iterator='lt'})")
c1("t:select({3}, {iterator='gt'})")
c1("t:select({3}, {iterator='eq'})")
c1("t:replace{3, 'new'}")
c1("t:select({3}, {iterator='ge'})")
c1("t:select({3}, {iterator='lt'})")
c1("t:select({3}, {iterator='gt'})")
c1("t:select({3}, {iterator='eq'})")
c1("t:delete{3}")
c1("t:select({3}, {iterator='ge'})")
c1("t:select({3}, {iterator='lt'})")
c1("t:select({3}, {iterator='gt'})")
c1("t:select({3}, {iterator='eq'})")
c1("t:replace{3}")
c1("t:delete{2}")
c1("t:select({3}, {iterator='lt'})")
c1("t:select({3}, {iterator='le'})")
c1("t:replace{2}")
c1("t:delete{1}")
c1("t:select({3}, {iterator='lt'})")
c1("t:select({3}, {iterator='le'})")
c1("t:delete{3}")
c1("t:select({3}, {iterator='lt'})")
c1("t:select({3}, {iterator='le'})")
c1:rollback()
c1("t:select{}")
--
--
-- Check that a cursor is closed automatically when a transaction
-- is committed or rolled back
--
c1:begin()
c1("t:select{1}")
c1("for k, v in box.space.test:pairs{} do box.commit() end")
c1:rollback()
c1:begin()
c1("t:select{1}")
c1("for k, v in box.space.test:pairs{} do box.rollback() end")
c1:rollback()
t:truncate()

--
-- Check that min/max/count transactions stay within a read view
--
t:replace{1}
c1:begin()
c1("t.index.pk:max()") -- {1}
c1("t.index.pk:min()") -- {1}
c1("t.index.pk:count()") -- 1
c2:begin()
c2("t:replace{2}") -- conflicts with c1 so c1 starts using a read view
c2:commit()
c1("t.index.pk:max()") -- {1}
c1("t.index.pk:min()") -- {1}
c1("t.index.pk:count()") -- 1
c1:commit()
--
-- Convert the reader to a read view: in this test we have
-- an explicit conflict between c1 and c2, so c1 begins
-- using a read view
--
c1:begin()
c1("t.index.pk:max()") -- {2}
c1("t.index.pk:min()") -- {1}
c1("t.index.pk:count()") -- 2
c2:begin()
c2("t:replace{1, 'new'}") -- conflits with c1 so c1 starts using a read view
c2("t:replace{3}")
c2:commit()
c1("t.index.pk:max()") -- {2}
c1("t.index.pk:min()") -- {1}
c1("t.index.pk:count()") -- 2
c1:commit()
t:truncate()

--
-- Check that select() does not add the key following
-- the last returned key to the conflict manager.
--
t:replace{1}
t:replace{2}
c1:begin()
c1("t:select({}, {limit = 0})") -- none
c2:begin()
c2("t:replace{1, 'new'}")
c2:commit()
c1("t:select({}, {limit = 1})") -- {1, 'new'}
c2:begin()
c2("t:replace{2, 'new'}")
c2:commit()
c1("t:select{}") -- {1, 'new'}, {2, 'new'}
c1:commit()
t:truncate()

--
-- gh-2716 uniqueness check for secondary indexes
--
_ = t:create_index('sk', {parts = {2, 'unsigned'}, unique = true})
c1:begin()
c2:begin()
c1("t:insert{1, 2}")
c2("t:insert{2, 2}")
c1:commit()
c2:commit() -- rollback
t:select{} -- {1, 2}
t:truncate()
t.index.sk:drop()

-- *************************************************************************
-- 1.7 cleanup marker: end of tests cleanup
-- *************************************************************************
--
box.space.test:drop()
c1 = nil
c2 = nil
c3 = nil
c4 = nil
c5 = nil
c6 = nil
c7 = nil
collectgarbage()

-- check of read views proper allocation/deallocation
s = box.schema.space.create('test', {engine = 'vinyl'})
i = box.space.test:create_index('pk')
s:replace{1, 2, 3}
s:replace{4, 5, 6}
s:replace{7, 8, 9}

box.stat.vinyl().tx.read_views -- 0 (no read views needed)
box.stat.vinyl().tx.transactions -- 0

c1 = txn_proxy.new()
c2 = txn_proxy.new()
c3 = txn_proxy.new()
c4 = txn_proxy.new()

c1:begin()
c2:begin()
c3:begin()
c4:begin()

box.stat.vinyl().tx.read_views -- 0 (no read views needed)
box.stat.vinyl().tx.transactions -- 0

c1("s:select{1}")
c2("s:select{1}")
c3("s:select{1}")
c4("s:select{1}")

box.stat.vinyl().tx.read_views -- 0 (no read views needed)
box.stat.vinyl().tx.transactions -- 4

c4("s:replace{1, 0, 0}")

box.stat.vinyl().tx.read_views -- 0 (no read views needed)
box.stat.vinyl().tx.transactions -- 4

c4:commit()

box.stat.vinyl().tx.read_views -- 1 (one read view for all TXs)
box.stat.vinyl().tx.transactions -- 3

c1:commit()

box.stat.vinyl().tx.read_views -- 1 (one read view for all TXs)
box.stat.vinyl().tx.transactions -- 2

c2:rollback()

box.stat.vinyl().tx.read_views -- 1 (one read view for all TXs)
box.stat.vinyl().tx.transactions -- 1

c3:commit()

box.stat.vinyl().tx.read_views -- 0 (no read views needed)
box.stat.vinyl().tx.transactions -- 0 (all done)

s:drop()
