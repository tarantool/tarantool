local digest = require('digest')

local PAGE_SIZE = 1024
local RANGE_SIZE = 64 * PAGE_SIZE
local TUPLE_SIZE = 128 * PAGE_SIZE

local function prepare()
    local s1 = box.schema.space.create('large_s1', { engine = 'vinyl', if_not_exists = true })
    s1:create_index('pk', {
        if_not_exists = true;
        range_size = RANGE_SIZE;
        page_size = PAGE_SIZE;
    })
end

local function large_test(iter_limit)
    iter_limit = iter_limit or 500

    local data = digest.urandom(TUPLE_SIZE)
    for i=0,iter_limit do
        local space = box.space.large_s1
        space:replace({i, data})
        if i % 100 == 0 then
            collectgarbage('collect')
        end
    end
end

local function check_test()
    local i = 0
    for _, tuple in box.space.large_s1:pairs() do
        if TUPLE_SIZE ~= tuple[2]:len() then
            error('Large tuple has incorect length')
        end
        if i % 10 == 0 then
            collectgarbage('collect')
        end
        i = i + 1
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
