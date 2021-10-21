#!/usr/bin/env tarantool

if arg[1] == 'replace' then
    box.ctl.on_schema_init(function()
        -- luacheck: ignore
        box.space._index:on_replace(function(old_space, new_space)
            if new_space[1] == 512 then
                box.space.test:on_replace(function(old_tup, new_tup)
                    box.space.temp:replace({1})
                    box.space.temp:replace({1})
                    box.space.loc:replace({1})
                    box.space.loc:replace({1})
                end)
            end
        end)
    end)
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv("LISTEN"),
})
