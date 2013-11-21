-- session-storage.lua

setmetatable(box.session, {
    __index = function(tbl, idx)

        if idx ~= 'storage' then
            return
        end

        local sid = box.session.id()

        local mt = getmetatable(tbl)

        if mt.aggregate_storage[ sid ] == nil then
            mt.aggregate_storage[ sid ] = {}
        end
        return mt.aggregate_storage[ sid ]
    end,

    aggregate_storage = {}
})
