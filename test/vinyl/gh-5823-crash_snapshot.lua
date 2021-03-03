#!/usr/bin/env tarantool

--
-- mode == 1: casual bootstrap;
-- mode == 2: casual bootstrap and fill in data.
--
local mode = tonumber(arg[1])
box.cfg ({
})

if mode == 2 then
    local v = box.schema.space.create('test_v', {engine = 'vinyl'})
    v:create_index('pk')
    local m = box.schema.space.create('test_m')
    m:create_index('pk')
    local str = string.rep('!', 100)
    for i = 1,10 do v:insert{i, str} end
    for i = 1,10 do m:insert{i, str} end
    box.error.injection.set("ERRINJ_SNAP_COMMIT_FAIL", true);
    box.snapshot()
end

require('console').listen(os.getenv('ADMIN'))
