-- {name, top, bottom, fifo...}
fifomax = 5
function find_or_create_fifo(space, name)
    fifo = space:get{name}
    if fifo == nil then
        fifo = {}
        for i = 1, fifomax do table.insert(fifo, 0) end
        fifo = space:insert{name, 4, 4, unpack(fifo)}
    end
    return fifo
end
function fifo_push(space, name, val)
    fifo = find_or_create_fifo(space, name)
    top = fifo[2]
    bottom = fifo[3]
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
function fifo_top(space, name)
    fifo = find_or_create_fifo(space, name)
    top = fifo[2]
    return fifo[top]
end
