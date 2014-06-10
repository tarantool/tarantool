#!/usr/bin/env tarantool
--
-- Test floating points values (too_long_treshold) with fractional part
--
box.cfg{
    admin_port = 3313,
    primary_port = 3314,
    slab_alloc_arena = 0.1,
    pid_file = "box.pid",
    rows_per_wal = 50,
    too_long_threshold = 0.01
}

s = box.schema.create_space('space1')
s:create_index('primary', {type = 'hash', parts = {0, 'NUM'}}) 

t = {}
for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t,k..':'..tostring(v)) end end

print('box.cfg')
for k,v in pairs(t) do print(k, v) end
print('------------------------------------------------------')
x = box.cfg.too_long_threshold
print('Check that too_long_threshold = 0.01')
print(x)
t = nil
s:drop()
os.exit()

