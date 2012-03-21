fifomax = 5
function find_or_create_fifo(name)
    fifo = box.select(0, 0, name)
    if fifo == nil then
        fifo = {}
        for i = 1, fifomax do fifo[i] = 0 end
        fifo = box.insert(0, name, 3, 3, unpack(fifo))
    end
    return fifo
end
function fifo_push(name, val)
    fifo = find_or_create_fifo(name)
    top = box.unpack('i', fifo[1])
    bottom = box.unpack('i', fifo[2])
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

    return box.update(0,
                      {
                          {
                              op = box.update_ops.ASSIGN,
                              field = 1,
                              value = top
                          },
                          {
                              op = box.update_ops.ASSIGN,
                              field = 2,
                              value = bottom
                          },
                          {
                              op = box.update_ops.ASSIGN,
                              field = top,
                              value = val
                          },
                      },
                      name)
end
function fifo_top(name)
    fifo = find_or_create_fifo(name)
    top = box.unpack('i', fifo[1])
    return box.unpack('i', fifo[top])
end
