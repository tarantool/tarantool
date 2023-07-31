local tabulate = require('internal.config.utils.tabulate')
local t = require('luatest')

local g = t.group()

g.test_basic = function()
    local header = {
        'Destination',
        'Gateway',
        'Genmask',
        'Flags',
        'Metric',
        'Ref',
        'Use',
        'Iface',
    }
    local route_1 = {
        '0.0.0.0',  -- Destination
        '10.0.0.1', -- Gateway
        '0.0.0.0',  -- Genmask
        'UG',       -- Flags
        '3005',     -- Metric
        '0',        -- Ref
        '0',        -- Use
        'wlan0',    -- Iface
    }
    local route_2 = {
        '10.0.0.0',      -- Destination
        '0.0.0.0',       -- Gateway
        '255.255.255.0', -- Genmask
        'U',             -- Flags
        '3005',          -- Metric
        '0',             -- Ref
        '0',             -- Use
        'wlan0',         -- Iface
    }
    local res = tabulate.encode({header, tabulate.SPACER, route_1, route_2})
    t.assert_equals(res, ([[
| Destination | Gateway  | Genmask       | Flags | Metric | Ref | Use | Iface |
| ----------- | -------- | ------------- | ----- | ------ | --- | --- | ----- |
| 0.0.0.0     | 10.0.0.1 | 0.0.0.0       | UG    | 3005   | 0   | 0   | wlan0 |
| 10.0.0.0    | 0.0.0.0  | 255.255.255.0 | U     | 3005   | 0   | 0   | wlan0 |
]]):lstrip())
end
