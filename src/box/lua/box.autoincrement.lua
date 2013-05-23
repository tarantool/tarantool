-- Assumes that spaceno has a TREE int32 (NUM) or int64 (NUM64) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
function box.auto_increment(spaceno, ...)
    spaceno = tonumber(spaceno)
    local max_tuple = box.space[spaceno].index[0].idx:max()
    local max = 0
    if max_tuple ~= nil then
        max = max_tuple[0]
        local fmt = 'i'
        if #max == 8 then fmt = 'l' end
        max = box.unpack(fmt, max)
    else
        -- first time
        if box.space[spaceno].index[0].key_field[0].type == "NUM64" then
            max = tonumber64(max)
        end
    end
    return box.insert(spaceno, max + 1, ...)
end


