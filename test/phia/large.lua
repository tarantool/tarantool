fiber = require('fiber')
digest = require('digest')

local function pcall_wrap(status, ...)
    if status ~= true then
        return false, tostring(...)
    end
    return status, ...
end;
local pcall_e = function(fun, ...)
    return pcall_wrap(pcall(fun, ...))
end;

box.once("phia_large", function()
    local s1 = box.schema.space.create('large_s1', { engine = 'phia', if_not_exists = true })
    s1:create_index('pk', {if_not_exists = true})
end)

local function t1(iter_limit, time_limit)
    local i = 0
    local t1 = fiber.time()
    local data = digest.urandom(2 * 1024 * 1024)
    while i < iter_limit and fiber.time() - t1 < time_limit do
	local space = box.space.large_s1
        space:replace({i, data})
	i = i + 1
    end
    return i
end

local function large_test(iter_limit, time_limit)
    iter_limit = iter_limit or 500
    time_limit = time_limit or 10

    return t1(iter_limit, time_limit)
end

local function check_test(iter_limit)
    iter_limit = iter_limit or box.space.large_s1.index[0]:len()
    math.randomseed(os.time())
    for _ = 1, 100 do
        local i = math.random(iter_limit) - 1
        if 2 * 1024 * 1024 ~= box.space.large_s1:get({i})[2]:len() then
	    error('Large tuple has incorect length')
        end
    end
end

return {
     large = large_test,
     check = check_test;
}
