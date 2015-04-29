-- session.lua

local session = box.session

setmetatable(session, {
    __index = function(tbl, idx)

        if idx ~= 'storage' then
            return
        end

        local sid = session.id()

        local mt = getmetatable(tbl)

        if mt.aggregate_storage[ sid ] == nil then
            mt.aggregate_storage[ sid ] = {}
        end
        return mt.aggregate_storage[ sid ]
    end,

    aggregate_storage = {}
})
