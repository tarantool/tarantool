#!/usr/bin/env tarantool

local function trig_local(old, new) -- luacheck: no unused args
    if new and new[3] == 'test_local' and new[6]['group_id'] ~= 1 then
        return new:update{{'=', 6, {group_id = 1}}}
    end
end

local function trig_engine(old, new) -- luacheck: no unused args
    if new and new[3] == 'test_engine' and new[4] ~= 'vinyl' then
        return new:update{{'=', 4, 'vinyl'}}
    end
end

box.ctl.on_schema_init(function()
    box.space._space:before_replace(trig_local)
    box.space._space:before_replace(trig_engine)
end)

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
})

require('console').listen(os.getenv('ADMIN'))
