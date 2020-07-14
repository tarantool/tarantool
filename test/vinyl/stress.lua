local fiber = require('fiber')

local function pcall_wrap(status, ...)
    if status ~= true then
        return false, tostring(...)
    end
    return status, ...
end;
local pcall_e = function(fun, ...)
    return pcall_wrap(pcall(fun, ...))
end;

box.once("vinyl_stress", function()
    local s1 = box.schema.space.create('s1', { engine = 'vinyl', if_not_exists = true })
    s1:create_index('pk', {if_not_exists = true})

    local s2 = box.schema.space.create('s2', { engine = 'vinyl', if_not_exists = true })
    s2:create_index('pk', {if_not_exists = true})

    local s3 = box.schema.space.create('s3', { engine = 'vinyl', if_not_exists = true })
    s3:create_index('pk', {if_not_exists = true})

    local s4 = box.schema.space.create('s4', { engine = 'vinyl', if_not_exists = true })
    s4:create_index('pk', {if_not_exists = true})

    local s5 = box.schema.space.create('s5', { engine = 'vinyl'})
    s5:create_index('pk')
end)

local spaces = {box.space.s1, box.space.s2, box.space.s3, box.space.s4,
    box.space.s5}

local max_data_size = box.cfg.vinyl_page_size * 1.5

local function t1(ch, time_limit)
    local t1 = fiber.time()
    while fiber.time() - t1 < time_limit do
        local k = math.random(10000)
        local t = math.random(80)
        local data = string.char(math.random(string.byte('Z') - string.byte('A')) + string.byte('A') - 1)
        data = data:rep(math.random(max_data_size))
        local space = spaces[math.fmod(t, #spaces) + 1]
        if t < 32 then
            space:replace({k, data})
        elseif t < 40 then
            space:upsert({k, data}, {{'=', 2, data}})
        elseif t < 56 then
            pcall_e(space.insert, space, {k, data})
        elseif t < 64 then
            space:delete({k})
        else
            pcall_e(space.update, space, {k}, {{'=', 2, data}})
        end
    end
    ch:put(1)
end;

local function t2(ch, time_limit)
    local t1 = fiber.time()
    while fiber.time() - t1 < time_limit do
        local k = math.random(10000)
        local t = math.random(16)
        local space = spaces[math.fmod(t, #spaces) + 1]
        if t < 12 then
            space:get({k})
        else
            space:delete({k})
        end
    end
    ch:put(2)
end;

local function t3(ch, time_limit)
    local t1 = fiber.time()
    local i = 0
    while fiber.time() - t1 < time_limit do
        i = i + 1
        local k = math.random(10000)
        local t = math.random(20)
        local l = math.random(2048)
        local space = spaces[math.fmod(t, #spaces) + 1]
        if t <= 6 then
            space:select(k, { iterator = 'GE', limit = l })
        elseif t <= 12 then
            space:select(k, { iterator = 'LE', limit = l })
        else
            space:delete({k})
        end
        if i % 10 == 0 then
            collectgarbage('collect')
        end
    end
    ch:put(3)
end;

local function stress(time_limit)
    time_limit = time_limit or 300
    local ch = fiber.channel(16)

    math.randomseed(os.time());

    for _ = 1, 6 do
        fiber.create(t1, ch, time_limit)
    end;

    for _ = 1, 6 do
        fiber.create(t2, ch, time_limit)
    end;

    for _ = 1, 4 do
        fiber.create(t3, ch, time_limit)
    end;

    for _ = 1, 16 do
        ch:get()
    end;
end

return {
     stress = stress;
}
