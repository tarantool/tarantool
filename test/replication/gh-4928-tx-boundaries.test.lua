-- gh-4928. Test that transactions mixing local and global
-- space operations are replicated correctly.
env = require('test_run')
test_run = env.new()
bit = require('bit')

-- Init.
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('glob')
_ = box.schema.space.create('loc', {is_local=true})
_ = box.space.glob:create_index('pk')
_ = box.space.loc:create_index('pk')

function gen_mixed_tx(i)\
    box.begin()\
    if bit.band(i, 1) ~= 0 then\
        box.space.glob:insert{10 * i + 1}\
    else\
        box.space.loc:insert{10 * i + 1}\
    end\
    if bit.band(i, 2) ~= 0 then\
        box.space.glob:insert{10 * i + 2}\
    else\
        box.space.loc:insert{10 * i + 2}\
    end\
    if bit.band(i, 4) ~= 0 then\
        box.space.glob:insert{10 * i + 3}\
    else\
        box.space.loc:insert{10 * i + 3}\
    end\
    box.commit()\
end

test_run:cmd("create server replica with rpl_master=default,\
             script='replication/replica.lua'")
test_run:cmd('start server replica')
test_run:wait_downstream(2, {status='follow'})

for i = 0, 7 do gen_mixed_tx(i) end

box.info.replication[2].status

vclock = box.info.vclock
vclock[0] = nil
test_run:wait_vclock("replica", vclock)

test_run:cmd('switch replica')

box.info.status
test_run:wait_upstream(1, {status = 'follow'})

box.space.glob:select{}

test_run:cmd('switch default')

-- Cleanup.
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.schema.user.revoke('guest', 'replication')
box.space.loc:drop()
box.space.glob:drop()
