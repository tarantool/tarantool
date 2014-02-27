fifomax = 5
function find_or_create_fifo(space, name)
    fifo = space:get{name}
    if fifo == nil then
        fifo = {}
        for i = 1, fifomax do fifo[i] = 0 end
        fifo = space:insert{name, 3, 3, unpack(fifo)}
    end
    return fifo
end
function fifo_push(space, name, val)
    fifo = find_or_create_fifo(space, name)
    top = fifo[1]
    bottom = fifo[2]
    if top == fifomax+2 then -- % size
        top = 3
    elseif top ~= bottom then -- was not empty
        top = top + 1
    end
    if bottom == fifomax + 2 then -- % size
        bottom = 3
    elseif bottom == top then
        bottom = bottom + 1
    end
    return space:update({name}, {{'=', 1, top}, {'=', 2, bottom}, {'=', top, val}})
end
function fifo_top(space, name)
    fifo = find_or_create_fifo(space, name)
    top = fifo[1]
    return fifo[top]
end
