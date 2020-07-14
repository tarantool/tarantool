-- {name, top, bottom, fifo...}
local fifomax = 5
local function find_or_create_fifo(space, name)
    local fifo = space:get{name}
    if fifo == nil then
        fifo = {}
        for _ = 1, fifomax do table.insert(fifo, 0) end
        fifo = space:insert{name, 4, 4, unpack(fifo)}
    end
    return fifo
end
local function fifo_push(space, name, val)
    local fifo = find_or_create_fifo(space, name)
    local top = fifo[2]
    local bottom = fifo[3]
    if top == fifomax+3 then -- % size
        top = 4
    elseif top ~= bottom then -- was not empty
        top = top + 1
    end
    if bottom == fifomax + 3 then -- % size
        bottom = 4
    elseif bottom == top then
        bottom = bottom + 1
    end
    return space:update({name}, {{'=', 2, top}, {'=', 3, bottom }, {'=', top, val}})
end
local function fifo_top(space, name)
    local fifo = find_or_create_fifo(space, name)
    local top = fifo[2]
    return fifo[top]
end

return {
    find_or_create_fifo = find_or_create_fifo,
    fifo_push = fifo_push,
    fifo_top = fifo_top,
    fifomax = fifomax,
};
