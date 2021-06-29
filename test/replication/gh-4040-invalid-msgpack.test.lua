test_run = require('test_run').new()

--
-- gh-4040. ER_INVALID_MSGPACK on a replica when master's relay times out after
-- not being able to write a full row to the socket.
--
test_run:cmd('create server master with script="replication/master1.lua"')
test_run:cmd('create server replica with rpl_master=master,\
                                         script="replication/replica_timeout.lua"')

test_run:cmd('start server master')
test_run:switch('master')
box.schema.user.grant('guest', 'replication')
box.cfg{replication_timeout=0.5}
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')

test_run:cmd('start server replica with args="1000"')
test_run:switch('replica')

sign = box.info.signature
box.error.injection.set('ERRINJ_APPLIER_READ_TX_ROW_DELAY', true)
test_run:switch('master')

-- Find the send buffer size. Testing uses Unix domain sockets. Create such a
-- socket and assume relay's socket has the same parameters.
socket = require('socket')
soc = socket('AF_UNIX', 'SOCK_STREAM', 0)
bufsize = soc:getsockopt('SOL_SOCKET', 'SO_SNDBUF')
require('log').info("SO_SNDBUF size is %d", bufsize)
-- Master shouldn't try to write the error while the socket isn't writeable.
box.error.injection.set('ERRINJ_IPROTO_WRITE_ERROR_DELAY', true)
-- Generate enough data to fill the sendbuf.
-- This will make the relay yield in the middle of writing a row waiting for the
-- socket to become writeable.
tbl = {1}
filler = string.rep('b', 100)
for i = 2, bufsize / 100 + 1 do\
    tbl[i] = filler\
end
for i = 1,10 do\
    tbl[1] = i\
    box.space.test:replace(tbl)\
end

-- Wait for the timeout to happen. The relay's send buffer should full by now
-- and contain a half-written row.
test_run:wait_downstream(2, {status='stopped'})

test_run:switch('replica')
-- Wait until replica starts receiving the data.
-- This will make master's socket writeable again.
box.error.injection.set('ERRINJ_APPLIER_READ_TX_ROW_DELAY', false)
test_run:wait_cond(function() return box.info.signature > sign end)

test_run:switch('master')
box.error.injection.set('ERRINJ_IPROTO_WRITE_ERROR_DELAY', false)

test_run:switch('replica')

-- There shouldn't be any errors other than the connection reset.
test_run:wait_upstream(1, {status='disconnected', message_re='unexpected EOF'})
assert(test_run:grep_log('replica', 'ER_INVALID_MSGPACK') == nil)

-- Cleanup.
test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('stop server master')
test_run:cmd('delete server replica')
test_run:cmd('delete server master')

