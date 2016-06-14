fiber = require('fiber')
digest = require('digest')

local function prepare()
    local s1 = box.schema.space.create('large_s1', { engine = 'phia', if_not_exists = true })
    s1:create_index('pk', {if_not_exists = true})
end

local function large_test(iter_limit, time_limit)
    iter_limit = iter_limit or 500
    time_limit = time_limit or 5

    local i = 0
    local t1 = fiber.time()
    local data = digest.urandom(2 * 1024 * 1024)
    while i < iter_limit and fiber.time() - t1 < time_limit do
        local space = box.space.large_s1
        space:replace({i, data})
        i = i + 1
    end
end

local function check_test()
    for _, tuple in box.space.large_s1:pairs() do
        if 2 * 1024 * 1024 ~= tuple[2]:len() then
            error('Large tuple has incorect length')
        end
    end
end

local function teardown()
    box.space.large_s1:drop()
end

return {
    prepare = prepare;
    large = large_test,
    check = check_test;
    teardown = teardown;
}
