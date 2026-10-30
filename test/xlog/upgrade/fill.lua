---
--- A script to generate some dataset used by migration.test.lua
---

box.cfg{ wal_max_size = 250 }
box.schema.space.create("distro")
box.space.distro:create_index('primary', { type = 'hash', unique = true,
    parts = {1, 'str', 2, 'str', 3, 'num'}})
box.space.distro:create_index('codename', { type = 'hash', unique = true,
    parts = {2, 'str'}})
box.space.distro:create_index('time', { type = 'tree', unique = false,
    parts = {4, 'num'}})

local function d(year, month, day)
    return os.time { year = year, month = month, day = day }
end

box.space.distro:insert({'debian', 'jessie', 80, d(2015, 4, 26)})
box.space.distro:insert({'debian', 'wheezy', 70, d(2013, 5, 04)})
box.space.distro:insert({'debian', 'squeeze', 60, d(2011, 2, 05)})
box.space.distro:insert({'debian', 'lenny', 50, d(2009, 2, 14)})
box.space.distro:insert({'debian', 'etch', 40, d(2007, 4, 8)})
box.space.distro:insert({'debian', 'sarge', 31, d(2005, 6, 6)})
box.space.distro:insert({'debian', 'woody', 30, d(2002, 7, 19)})
box.space.distro:insert({'ubuntu', 'wily', 1510, d(2015, 10, 22)})
box.space.distro:insert({'ubuntu', 'vivid', 1504, d(2015, 4, 23)})
box.space.distro:insert({'ubuntu', 'trusty', 1404, d(2014, 4, 17)})
box.space.distro:insert({'ubuntu', 'precise', 1510, d(2012, 4, 26)})

-- 1.6.5+
if box.space.distro.format ~= nil then
    local format={}
    format[1] = {name='os', type='str'}
    format[2] = {name='dist', type='str'}
    format[3] = {name='version', type='num'}
    format[4] = {name='time', type='num'}
    box.space.distro:format(format)
end

box.schema.space.create('temporary', { temporary = true })
box.schema.user.create('someuser', { password  = 'somepassword' })
box.schema.user.grant('someuser', 'read,write', 'universe')
box.session.su('someuser')
box.schema.func.create('somefunc', { setuid = true })
box.session.su('admin')
box.schema.user.revoke('someuser', 'read,write', 'universe')
box.schema.role.create('somerole')
box.schema.user.grant('someuser', 'execute', 'role', 'somerole')
if _TARANTOOL == nil or  _TARANTOOL < "1.6.6" then
    box.schema.user.grant('somerole', 'read,write', 'space', 'distro')
    box.schema.user.grant('public', 'execute', 'function', 'somefunc')
else
    box.schema.role.grant('somerole', 'read,write', 'space', 'distro')
    box.schema.role.grant('public', 'execute', 'function', 'somefunc')
end
box.schema.func.create('someotherfunc')
box.schema.user.grant('someuser', 'execute', 'function', 'someotherfunc')
box.schema.user.grant('someuser', 'read,write', 'space', 'temporary')

box.schema.upgrade()
box.snapshot()

os.exit(0)
