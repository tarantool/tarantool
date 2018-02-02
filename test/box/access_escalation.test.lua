fiber = require('fiber')
net = require('net.box')
log = require('log')
json = require('json')
os = require('os')

-- gh-617: guest access denied because of setuid
-- function invocation.

-- Test for privilege escalation
-- -----------------------------
-- * create a setuid function which changes effective id
--   to superuser
-- * invoke it via the binary protocol
-- * while the function is running, invoke a non-setuid function
--   which reads a system space.
--
--  The invoked function should get "Access denied" error,
--  there should be no privilege escalation.

-- define functions

function setuid() fiber.sleep(1000000) end

function escalation() return box.space._space:get{box.schema.SPACE_ID} ~= nil end

-- set up grants

box.schema.func.create('setuid', {setuid=true})
box.schema.func.create('escalation')

box.schema.user.grant('guest', 'execute', 'function', 'setuid')
box.schema.user.grant('guest', 'execute', 'function', 'escalation')


connection = net:connect(os.getenv("LISTEN"))

background = fiber.create(function() connection:call("setuid") end)
connection:call("escalation")
fiber.cancel(background)

--
-- tear down the functions; the grants are dropped recursively
--

box.schema.func.drop('setuid')
box.schema.func.drop('escalation')

connection:close()

-- Test for privilege de-escalation
-- --------------------------------

--
-- * create a setuid function which runs under a deprived user
-- * invoke the function, let it sleep
-- * invoke a function which should have privileges
-- 

-- create a deprived user

box.schema.user.create('underprivileged')
box.schema.user.grant('underprivileged', 'read,write', 'space', '_func')
box.session.su('underprivileged')
box.schema.func.create('setuid', {setuid=true})
box.session.su('admin')
--
-- create a deprived function
--

box.schema.user.grant('guest', 'read,write,execute', 'universe')

connection = net:connect(os.getenv("LISTEN"))

background = fiber.create(function() connection:call("setuid") end)
connection:call("escalation")
fiber.cancel(background)

-- tear down

box.schema.user.drop('underprivileged')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

connection:close()
