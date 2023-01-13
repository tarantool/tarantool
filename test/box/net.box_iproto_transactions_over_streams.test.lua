-- This test checks streams iplementation in iproto (gh-5860).
net_box = require('net.box')
json = require('json')
fiber = require('fiber')
msgpack = require('msgpack')
test_run = require('test_run').new()

test_run:cmd("create server test with script='box/iproto_streams.lua'")

test_run:cmd("setopt delimiter ';'")
function get_current_connection_count()
    local total_net_stat_table =
        test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table)
    local connection_stat_table = total_net_stat_table.CONNECTIONS
    assert(connection_stat_table)
    return connection_stat_table.current
end;
function get_current_stream_count()
    local total_net_stat_table =
        test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table)
    local stream_stat_table = total_net_stat_table.STREAMS
    assert(stream_stat_table)
    return stream_stat_table.current
end;
function get_current_msg_count()
    local total_net_stat_table =
        test_run:cmd(string.format("eval test 'return box.stat.net()'"))[1]
    assert(total_net_stat_table)
    local request_stat_table = total_net_stat_table.REQUESTS
    assert(request_stat_table)
    return request_stat_table.current
end;
function wait_and_return_results(futures)
    local results = {}
    for name, future in pairs(futures) do
        local err
        results[name], err = future:wait_result()
        if err then
            results[name] = err
        end
    end
    return results
end;
function create_remote_space()
    test_run:eval('test', "s = box.schema.space.create('test')")
    test_run:eval('test', "s:create_index('primary')")
end;
function drop_remote_space()
    test_run:eval('test', "s:drop()")
end;
function start_server_and_init(mvcc_is_enable)
    test_run:cmd(
        string.format("start server test with args='10, %s'", mvcc_is_enable)
    )
    local server_addr =
        test_run:cmd("eval test 'return box.cfg.listen'")[1]
    local net_msg_max =
        test_run:cmd("eval test 'return box.cfg.net_msg_max'")[1]
    create_remote_space()
    local conn = net_box.connect(server_addr)
    local stream_1 = conn:new_stream()
    local stream_2 = conn:new_stream()
    local space_1_1 = stream_1.space.test
    local space_1_2 = stream_2.space.test
    return conn, stream_1, space_1_1, stream_2, space_1_2, net_msg_max
end;
function cleanup_and_stop_server(connection)
    drop_remote_space()
    connection:close()
    test_run:wait_cond(function () return get_current_connection_count() == 0 end)
    test_run:cmd("stop server test")
end;
test_run:cmd("setopt delimiter ''");

test_run:cmd("start server test with args='1'")
server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]

-- Simple checks for transactions
conn_1 = net_box.connect(server_addr)
conn_2 = net_box.connect(server_addr)
stream_1_1 = conn_1:new_stream()
stream_1_2 = conn_1:new_stream()
stream_2 = conn_2:new_stream()
-- It's ok to commit or rollback without any active transaction
stream_1_1:commit()
stream_1_1:rollback()

stream_1_1:begin()
-- Error unable to start second transaction in one stream
stream_1_1:begin()
-- It's ok to start transaction in separate stream in one connection
stream_1_2:begin()
-- It's ok to start transaction in separate stream in other connection
stream_2:begin()
test_run:switch("test")
-- It's ok to start local transaction separately with active stream
-- transactions
box.begin()
box.commit()
test_run:switch("default")
stream_1_1:commit()
stream_1_2:commit()
stream_2:commit()

-- Check unsupported requests
conn = net_box.connect(server_addr)
assert(conn:ping())
-- Begin, commit and rollback supported only for streams
conn:_request(net_box._method.begin, nil, nil, nil)
conn:_request(net_box._method.commit, nil, nil, nil)
conn:_request(net_box._method.rollback, nil, nil, nil)
-- Not all requests supported by stream.
stream = conn:new_stream()
-- Start transaction to allocate stream object on the
-- server side
stream:begin()
IPROTO_REQUEST_TYPE       = 0x00
IPROTO_SYNC               = 0x01
IPROTO_AUTH               = 7
IPROTO_STREAM_ID          = 0x0a
test_run:cmd("setopt delimiter ';'")
header = msgpack.encode({
    [IPROTO_REQUEST_TYPE] = IPROTO_AUTH,
    [IPROTO_SYNC] = conn:_next_sync(),
    [IPROTO_STREAM_ID] = 1,
});
body = msgpack.encode({nil});
size = msgpack.encode(header:len() + body:len());
conn:_request(net_box._method.inject, nil, nil, nil, size .. header .. body);
test_run:cmd("setopt delimiter ''");
conn:close()
test_run:cmd("stop server test")

-- Second argument (false is a value for memtx_use_mvcc_engine option)
-- Server start without active transaction manager, so all transaction
-- fails because of yeild!
conn, stream, space = start_server_and_init(false)

-- Check syncronious stream txn requests for memtx
-- with memtx_use_mvcc_engine = false
stream:begin()
test_run:wait_cond(function () return get_current_stream_count() == 1 end)
space:replace({1})
-- Select fails, because memtx_use_mvcc_engine is false
space:select({})
test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
-- Commit fails, transaction yeild with memtx_use_mvcc_engine = false
stream:commit()
-- Select is empty, transaction was aborted
space:select{}
-- Check that after failed transaction commit we able to start next
-- transaction (it's strange check, but it's necessary because it was
-- bug with it)
stream:begin()
stream:ping()
stream:commit()
-- Same checks for `call` end `eval` functions.
stream:call('box.begin')
stream:call('s:replace', {{1}})
-- Select fails, because memtx_use_mvcc_engine is false
space:select({})
stream:call('s:select', {})
test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
-- Commit fails, transaction yeild with memtx_use_mvcc_engine = false
stream:eval('box.commit()')
-- Select is empty, transaction was aborted
space:select{}

-- Same checks for `execute` function which can also
-- begin and commit transaction.
stream:execute('START TRANSACTION')
stream:call('s:replace', {{1}})
-- Select fails, because memtx_use_mvcc_engine is false
space:select({})
stream:call('s:select', {})
test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
-- Commit fails, transaction yeild with memtx_use_mvcc_engine = false
stream:execute('COMMIT')
-- Select is empty, transaction was aborted
space:select{}
-- Check that there are no streams and messages, which
-- was not deleted
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

cleanup_and_stop_server(stream)

-- Next we check transactions only for memtx with memtx_use_mvcc_engine = true,
-- because if memtx_use_mvcc_engine == false all transactions fails, as we can
-- see before!

-- Second argument (true is a value for memtx_use_mvcc_engine option)
-- Same test case as previous but server start with active transaction
-- manager.
conn, stream, space = start_server_and_init(true)
-- Spaces getting from connection, not from stream has no stream_id
-- and not belongs to stream
space_no_stream = conn.space.test

stream:begin()
space:replace({1})
test_run:wait_cond(function() return get_current_stream_count() == 1 end)
-- Empty select, transaction was not commited and
-- is not visible from requests not belonging to the
-- transaction.
space_no_stream:select{}
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space:select({})
test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
-- Commit was successful, transaction can yeild with
-- memtx_use_mvcc_engine = true.
stream:commit()
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

-- Select return tuple, which was previously inserted,
-- because transaction was successful
space:select{}
test_run:switch("test")
-- Select return tuple, which was previously inserted,
-- because transaction was successful
s:select()
test_run:switch('default')
cleanup_and_stop_server(conn)


-- Check conflict resolution in stream transactions,
conn, stream_1, space_1_1, stream_2, space_1_2 = start_server_and_init(true)

stream_1:begin()
stream_2:begin()

-- Simple read/write conflict.
space_1_1:select({1})
space_1_2:select({1})
space_1_1:replace({1, 1})
space_1_2:replace({1, 2})
stream_1:commit()
-- This transaction fails, because of conflict
stream_2:commit()
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

-- Here we must accept [1, 1]
space_1_1:select({})
space_1_2:select({})

test_run:switch('test')
-- Both select return tuple [1, 1], transaction commited
s:select()
test_run:switch('default')
cleanup_and_stop_server(conn)

-- Check rollback as a command
conn, stream, space = start_server_and_init(true)
stream:begin()

-- Test rollback for memtx space
space:replace({1})
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space:select({})
stream:rollback()
-- Select is empty, transaction rollback
space:select({})

test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

-- This is simple test is necessary because i have a bug
-- with halting stream after rollback
stream:begin()
stream:commit()
conn:close()

test_run:switch('test')
-- Both select are empty, because transaction rollback
s:select()
test_run:switch('default')
cleanup_and_stop_server(conn)

-- Check rollback on disconnect
conn, stream, space = start_server_and_init(true)
stream:begin()

space:replace({1})
space:replace({2})
-- Select return two previously inserted tuples
space:select({})
conn:close()

-- Check that there are no streams and messages, which
-- was not deleted after connection close
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)
test_run:wait_cond(function () return get_current_connection_count() == 0 end)

test_run:switch("test")
-- Empty select, transaction was rollback
s:select()
test_run:switch("default")
cleanup_and_stop_server(conn)

-- Check rollback on disconnect with big count of async requests
conn, stream, space, _, _, net_msg_max = start_server_and_init(true)
stream:begin()

space:replace({1})
space:replace({2})
-- Select return two previously inserted tuples
space:select({})

-- We send a large number of asynchronous requests,
-- their result is not important to us, it is important
-- that they will be in the stream queue at the time of
-- the disconnect.
test_run:cmd("setopt delimiter ';'")
for i = 1, net_msg_max * 2 do
    space:replace({i}, {is_async = true})
end;
test_run:cmd("setopt delimiter ''");
conn:close()

-- Check that there are no streams and messages, which
-- was not deleted after connection close
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)
test_run:wait_cond(function () return get_current_connection_count() == 0 end)


test_run:switch("test")
-- Select was empty, transaction rollbacked
s:select()
test_run:switch("default")

-- Same test, but now we check that if `commit` was received
-- by server before connection closed, we processed it successful.
conn = net_box.connect(server_addr)
stream = conn:new_stream()
space = stream.space.test
stream:begin()
test_run:cmd("setopt delimiter ';'")
-- Here, for a large number of messages, we cannot guarantee their processing,
-- since if the net_msg_max limit is reached, we will stop processing incoming
-- requests, and after close, we will discard all raw data.
for i = 1, net_msg_max - 3 do
    space:replace({i}, {is_async = true})
end
_ = stream:commit({is_async = true})
test_run:cmd("setopt delimiter ''");
conn:close()

-- Check that there are no streams and messages, which
-- was not deleted after connection close
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)
test_run:wait_cond(function () return get_current_connection_count() == 0 end)

test_run:switch("test")
-- Select return tuples from [1] to [100],
-- transaction was commit
rc = s:select()
assert(#rc)
test_run:switch("default")
cleanup_and_stop_server(conn)

-- Check that all requests between `begin` and `commit`
-- have correct lsn and tsn values. During my work on the
-- patch, i see that all requests in stream comes with
-- header->is_commit == true, so if we are in transaction
-- in stream we should set this value to false, otherwise
-- during recovering `wal_stream_apply_dml_row` fails, because
-- of LSN/TSN mismatch. Here is a special test case for it.
conn, stream, space = start_server_and_init(true)

stream:begin()
space:replace({1})
space:replace({2})
stream:commit()

-- Check that there are no streams and messages, which
-- was not deleted
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

test_run:switch('test')
-- Here we get two tuples, commit was successful
s:select{}
test_run:switch('default')
conn:close()
test_run:wait_cond(function () return get_current_connection_count() == 0 end)
test_run:cmd("stop server test")

test_run:cmd("start server test with args='1, true'")
test_run:switch('test')
-- Here we get two tuples, commit was successful
box.space.test:select{}
box.space.test:drop()
test_run:switch('default')
test_run:cmd("stop server test")

-- Same transactions checks for async mode
conn, stream, space = start_server_and_init(true)

futures = {}
futures["begin"] = stream:begin({is_async = true})
futures["replace"] = space:replace({1}, {is_async = true})
futures["insert"] = space:insert({2}, {is_async = true})
futures["select"] = space:select({}, {is_async = true})

test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
futures["commit"] = stream:commit({is_async = true})

results = wait_and_return_results(futures)
-- If begin was successful it return nil
assert(not results["begin"])
-- [1]
assert(results["replace"])
-- [2]
assert(results["insert"])
-- [1] [2]
assert(results["select"])
-- If commit was successful it return nil
assert(not results["commit"])

test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

test_run:switch("test")
-- Select return tuples, which was previously inserted,
-- because transaction was successful
s:select()
test_run:switch('default')
cleanup_and_stop_server(conn)

-- Check conflict resolution in stream transactions,
conn, stream_1, space_1_1, stream_2, space_1_2 = start_server_and_init(true)
futures = {}
-- Simple read/write conflict.
futures["begin_1"] = stream_1:begin({is_async = true})
futures["begin_2"] = stream_2:begin({is_async = true})
futures["select_1_1"] = space_1_1:select({1}, {is_async = true})
futures["select_1_2"] = space_1_2:select({1}, {is_async = true})
futures["replace_1_1"] = space_1_1:replace({1, 1}, {is_async = true})
futures["replace_1_2"] = space_1_2:replace({1, 2}, {is_async = true})
futures["commit_1"] = stream_1:commit({is_async = true})
futures["commit_2"] = stream_2:commit({is_async = true})
futures["select_1_1_A"] = space_1_1:select({}, {is_async = true})
futures["select_1_2_A"] = space_1_2:select({}, {is_async = true})

results = wait_and_return_results(futures)
-- Successful begin return nil
assert(not results["begin_1"])
assert(not results["begin_2"])
-- []
assert(not results["select_1_1"][1])
assert(not results["select_1_2"][1])
-- [1]
assert(results["replace_1_1"][1])
-- [1]
assert(results["replace_1_1"][2])
-- [1]
assert(results["replace_1_2"][1])
-- [2]
assert(results["replace_1_2"][2])
-- Successful commit return nil
assert(not results["commit_1"])
-- Error because of transaction conflict
assert(results["commit_2"])
-- [1, 1]
assert(results["select_1_1_A"][1])
-- commit_1 could have ended before commit_2, so
-- here we can get both empty select and [1, 1]
-- for results_1["select_1_2_A"][1]

test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

test_run:switch('test')
-- Select return tuple [1, 1], transaction commited
s:select()
test_run:switch('default')
cleanup_and_stop_server(conn)

-- Checks for iproto call/eval/execute in stream
conn, stream, space = start_server_and_init(true)
space_no_stream = conn.space.test

test_run:switch("test")
function ping() return "pong" end
test_run:switch('default')

-- successful begin using stream:call
stream:call('box.begin')
-- error: Operation is not permitted when there is an active transaction
stream:eval('box.begin()')
-- error: Operation is not permitted when there is an active transaction
stream:begin()
-- error: Operation is not permitted when there is an active transaction
stream:execute('START TRANSACTION')
stream:call('ping')
stream:eval('ping()')
-- error: Operation is not permitted when there is an active transaction
stream:call('box.begin')
stream:eval('box.begin()')
-- successful commit using stream:call
stream:call('box.commit')

-- successful begin using stream:eval
stream:eval('box.begin()')
space:replace({1})
-- Empty select, transaction was not commited and
-- is not visible from requests not belonging to the
-- transaction.
space_no_stream:select{}
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space:select({})
test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
--Successful commit using stream:execute
stream:execute('COMMIT')
-- Select return tuple, which was previously inserted,
-- because transaction was successful
space_no_stream:select{}
test_run:switch("test")
-- Select return tuple, because transaction was successful
s:select()
s:delete{1}
test_run:switch('default')
-- Check rollback using stream:call
stream:begin()
space:replace({2})
-- Empty select, transaction was not commited and
-- is not visible from requests not belonging to the
-- transaction.
space_no_stream:select{}
-- Select return tuple, which was previously inserted,
-- because this select belongs to transaction.
space:select({})
test_run:switch("test")
-- Select is empty, transaction was not commited
s:select()
test_run:switch('default')
--Successful rollback using stream:call
stream:call('box.rollback')
-- Empty selects transaction rollbacked
space:select({})
space_no_stream:select{}

test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)

test_run:switch("test")
-- Empty select transaction rollbacked
s:select()
test_run:switch('default')
cleanup_and_stop_server(conn)

-- Simple test which demostrates that stream immediately
-- destroyed, when no processing messages in stream and
-- no active transaction.
conn, stream, space = start_server_and_init(true)
for i = 1, 10 do space:replace{i} end
-- All messages was processed, so stream object was immediately
-- deleted, because no active transaction started.
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)
cleanup_and_stop_server(conn)

-- Transaction tests for sql iproto requests.
-- All this functions are copy-paste from sql/ddl.test.lua,
-- except that they check sql transactions in streams
test_run:cmd("setopt delimiter '$'")
function execute_sql_string(stream, sql_string)
    if stream then
        stream:execute(sql_string)
    else
        box.execute(sql_string)
    end
end$
function execute_sql_string_and_return_result(stream, sql_string)
    if stream then
        return pcall(stream.execute, stream, sql_string)
    else
        return box.execute(sql_string)
    end
end$
function monster_ddl(stream)
    local _, err1, err2, err3, err4, err5, err6
    local stream_or_box = stream or box
    execute_sql_string(stream, [[CREATE TABLE t1(id INTEGER PRIMARY KEY,
                                                 a INTEGER,
                                                 b INTEGER);]])
    execute_sql_string(stream, [[CREATE TABLE t2(id INTEGER PRIMARY KEY,
                                                 a INTEGER,
                                                 b INTEGER UNIQUE,
                                                 CONSTRAINT ck1
                                                 CHECK(b < 100));]])

    execute_sql_string(stream, 'CREATE INDEX t1a ON t1(a);')
    execute_sql_string(stream, 'CREATE INDEX t2a ON t2(a);')

    execute_sql_string(stream, [[CREATE TABLE t_to_rename(id INTEGER PRIMARY
                                                          KEY, a INTEGER);]])

    execute_sql_string(stream, 'DROP INDEX t2a ON t2;')

    execute_sql_string(stream, 'CREATE INDEX t_to_rename_a ON t_to_rename(a);')

    execute_sql_string(stream, [[ALTER TABLE t1 ADD CONSTRAINT ck1
                                 CHECK(b > 0);]])

    _, err1 =
        execute_sql_string_and_return_result(stream, [[ALTER TABLE t_to_rename
                                                       RENAME TO t1;]])

    execute_sql_string(stream, [[ALTER TABLE t1 ADD CONSTRAINT
                                 ck2 CHECK(a > 0);]])
    execute_sql_string(stream, 'ALTER TABLE t1 DROP CONSTRAINT ck1;')

    execute_sql_string(stream, [[ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY
                                 (a) REFERENCES t2(b);]])
    execute_sql_string(stream, 'ALTER TABLE t1 DROP CONSTRAINT fk1;')

    _, err2 =
        execute_sql_string_and_return_result(stream, [[CREATE TABLE t1(id
                                                       INTEGER PRIMARY KEY);]])

    execute_sql_string(stream, [[ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY
                                 (a) REFERENCES t2(b);]])

    execute_sql_string(stream, [[CREATE TABLE
                                 trigger_catcher(id INTEGER PRIMARY
                                                 KEY AUTOINCREMENT);]])

    execute_sql_string(stream, 'ALTER TABLE t_to_rename RENAME TO t_renamed;')

    execute_sql_string(stream, 'DROP INDEX t_to_rename_a ON t_renamed;')

    execute_sql_string(stream, [[CREATE TRIGGER t1t AFTER INSERT ON
                                 t1 FOR EACH ROW
                                 BEGIN
                                     INSERT INTO trigger_catcher VALUES(1);
                                 END; ]])

    _, err3 = execute_sql_string_and_return_result(stream, 'DROP TABLE t3;')

    execute_sql_string(stream, [[CREATE TRIGGER t2t AFTER INSERT ON
                                 t2 FOR EACH ROW
                                 BEGIN
                                     INSERT INTO trigger_catcher VALUES(1);
                                 END; ]])

    _, err4 =
        execute_sql_string_and_return_result(stream, [[CREATE INDEX t1a
                                                       ON t1(a, b);]])

    execute_sql_string(stream, 'TRUNCATE TABLE t1;')
    _, err5 =
        execute_sql_string_and_return_result(stream, 'TRUNCATE TABLE t2;')
    _, err6 =
        execute_sql_string_and_return_result(stream, [[TRUNCATE TABLE
                                                       t_does_not_exist;]])

    execute_sql_string(stream, 'DROP TRIGGER t2t;')

    return {'Finished ok, errors in the middle: ', err1, err2, err3, err4,
            err5, err6}
end$
function monster_ddl_cmp_res(res1, res2)
    if json.encode(res1) == json.encode(res2) then
        return true
    end
    return res1, res2
end$
function monster_ddl_is_clean(stream)
    local stream_or_box = stream or box
    assert(stream_or_box.space.T1 == nil)
    assert(stream_or_box.space.T2 == nil)
    assert(stream_or_box.space._trigger:count() == 0)
    assert(stream_or_box.space._fk_constraint:count() == 0)
    assert(stream_or_box.space._ck_constraint:count() == 0)
    assert(stream_or_box.space.T_RENAMED == nil)
    assert(stream_or_box.space.T_TO_RENAME == nil)
end$
function monster_ddl_check(stream)
    local _, err1, err2, err3, err4, res
    local stream_or_box = stream or box
    _, err1 =
       execute_sql_string_and_return_result(stream, [[INSERT INTO t2
                                                      VALUES (1, 1, 101)]])
    execute_sql_string(stream, 'INSERT INTO t2 VALUES (1, 1, 1)')
    _, err2 =
        execute_sql_string_and_return_result(stream, [[INSERT INTO t2
                                                       VALUES(2, 2, 1)]])
    _, err3 =
        execute_sql_string_and_return_result(stream, [[INSERT INTO t1
                                                       VALUES(1, 20, 1)]])
    _, err4 =
        execute_sql_string_and_return_result(stream, [[INSERT INTO t1
                                                       VALUES(1, -1, 1)]])
    execute_sql_string(stream, 'INSERT INTO t1 VALUES (1, 1, 1)')
    if not stream then
        assert(stream_or_box.space.T_RENAMED ~= nil)
        assert(stream_or_box.space.T_RENAMED.index.T_TO_RENAME_A == nil)
        assert(stream_or_box.space.T_TO_RENAME == nil)
        res = execute_sql_string_and_return_result(stream, [[SELECT * FROM
                                                             trigger_catcher]])
    else
        _, res =
            execute_sql_string_and_return_result(stream, [[SELECT * FROM
                                                           trigger_catcher]])
    end
    return {'Finished ok, errors and trigger catcher content: ', err1, err2,
            err3, err4, res}
end$
function monster_ddl_clear(stream)
    execute_sql_string(stream, 'DROP TRIGGER IF EXISTS t1t;')
    execute_sql_string(stream, 'DROP TABLE IF EXISTS trigger_catcher;')
    execute_sql_string(stream, 'ALTER TABLE t1 DROP CONSTRAINT fk1;')
    execute_sql_string(stream, 'DROP TABLE IF EXISTS t2')
    execute_sql_string(stream, 'DROP TABLE IF EXISTS t1')
    execute_sql_string(stream, 'DROP TABLE IF EXISTS t_renamed')
end$
test_run:cmd("setopt delimiter ''")$

test_run:cmd("start server test with args='10, true'")
test_run:switch('test')
test_run:cmd("setopt delimiter '$'")
function monster_ddl_is_clean()
    if not (box.space.T1 == nil) or
       not (box.space.T2 == nil) or
       not (box.space._trigger:count() == 0) or
       not (box.space._fk_constraint:count() == 0) or
       not (box.space._ck_constraint:count() == 0) or
       not (box.space.T_RENAMED == nil) or
       not (box.space.T_TO_RENAME == nil) then
           return false
    end
    return true
end$
test_run:cmd("setopt delimiter ''")$
test_run:switch('default')

server_addr = test_run:cmd("eval test 'return box.cfg.listen'")[1]
conn = net_box.connect(server_addr)
stream = conn:new_stream()

-- No txn.
true_ddl_res = monster_ddl()
true_ddl_res

true_check_res = monster_ddl_check()
true_check_res

monster_ddl_clear()
monster_ddl_is_clean()

-- Both DDL and cleanup in one txn in stream.
ddl_res = monster_ddl(stream)
stream:execute('START TRANSACTION')
check_res = monster_ddl_check(stream)
stream:execute('COMMIT')
monster_ddl_clear(stream)
monster_ddl_is_clean()

monster_ddl_cmp_res(ddl_res, true_ddl_res)
monster_ddl_cmp_res(check_res, true_check_res)

-- All messages was processed, so stream object was immediately
-- deleted, because no active transaction started.
test_run:wait_cond(function() return get_current_stream_count() == 0 end)
test_run:wait_cond(function() return get_current_msg_count() == 0 end)
conn:close()
test_run:wait_cond(function () return get_current_connection_count() == 0 end)


-- Check for prepare and unprepare functions
conn = net_box.connect(server_addr)
assert(conn:ping())
stream = conn:new_stream()

stream:execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT)')
-- reload schema
stream:ping()
space = stream.space.TEST
assert(space ~= nil)
stream:execute('START TRANSACTION')
space:replace{1, 2, '3'}
space:select()
-- select is empty, because transaction was not commited
conn.space.TEST:select()
stream_pr = stream:prepare("SELECT * FROM test WHERE id = ? AND a = ?;")
conn_pr = conn:prepare("SELECT * FROM test WHERE id = ? AND a = ?;")
assert(stream_pr.stmt_id == conn_pr.stmt_id)
-- [ 1, 2, '3' ]
stream:execute(stream_pr.stmt_id, {1, 2})
-- empty select, transaction was not commited
conn:execute(conn_pr.stmt_id, {1, 2})
stream:execute('COMMIT')
-- [ 1, 2, '3' ]
stream:execute(stream_pr.stmt_id, {1, 2})
-- [ 1, 2, '3' ]
conn:execute(conn_pr.stmt_id, {1, 2})
stream:unprepare(stream_pr.stmt_id)
conn:close()
test_run:switch('test')
-- [ 1, 2, '3' ]
box.space.TEST:select()
box.space.TEST:drop()
test_run:switch('default')
test_run:cmd("stop server test")

test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
