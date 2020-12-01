remote = require 'net.box'
fiber = require 'fiber'
log = require 'log'

LISTEN = require('uri').parse(box.cfg.listen)
space = box.schema.space.create('net_box_test_space')
index = space:create_index('primary', { type = 'tree' })

-- low level connection
log.info("create connection")
cn = remote.connect(LISTEN.host, LISTEN.service)
log.info("state is %s", cn.state)

cn:ping()
log.info("ping is done")
cn:ping()
log.info("ping is done")


cn:ping()


-- check permissions
cn:call('unexists_procedure')
function test_foo(a,b,c) return { {{ [a] = 1 }}, {{ [b] = 2 }}, c } end
cn:call('test_foo', {'a', 'b', 'c'})
cn:eval('return 2+2')
cn:close()
-- connect and call without usage access
box.schema.user.grant('guest','execute','universe')
box.schema.user.revoke('guest','usage','universe')
box.session.su("guest")
cn = remote.connect(LISTEN.host, LISTEN.service)
cn:call('test_foo', {'a', 'b', 'c'})
box.session.su("admin")
box.schema.user.grant('guest','usage','universe')
cn:close()
cn = remote.connect(box.cfg.listen)

cn:call('unexists_procedure')
cn:call('test_foo', {'a', 'b', 'c'})
cn:call(nil, {'a', 'b', 'c'})
cn:eval('return 2+2')
cn:eval('return 1, 2, 3')
cn:eval('return ...', {1, 2, 3})
cn:eval('return { k = "v1" }, true, {  xx = 10, yy = 15 }, nil')
cn:eval('return nil')
cn:eval('return')
cn:eval('error("exception")')
cn:eval('box.error(0)')
cn:eval('!invalid expression')

-- box.commit() missing at return of CALL/EVAL
-- see gh-2016 - now the behaviour is to silently rollback tx
function no_commit() box.begin() fiber.sleep(0.001) end
cn:call('no_commit')
cn:eval('no_commit()')

remote.self:eval('return 1+1, 2+2')
remote.self:eval('return')
remote.self:eval('error("exception")')
remote.self:eval('box.error(0)')
remote.self:eval('!invalid expression')

box.schema.user.revoke('guest', 'execute', 'universe')

cn:close()
