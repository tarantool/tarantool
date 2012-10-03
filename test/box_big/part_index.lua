function ok_trigger(tuple)
    return true
end

function false_trigger(tuple)
    return false
end

function less_than_10_trigger(tuple)
    if box.unpack('i', tuple[1]) < 10 then
        return true
    else
        return
    end
end

function cleanup_space(space)
    local list =  { box.select_range(space, 0, 0x7FFFFFF) }

    local deleted = 0
    for k, tuple in pairs(list) do
        box.delete(space, tuple[0])
        deleted = deleted + 1
    end
    return deleted
end
