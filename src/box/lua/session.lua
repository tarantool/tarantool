-- session.lua

local utils = require('internal.utils')

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

local SESSION_NEW_OPTS = {
    type = 'string',
    fd = 'number',
    user = 'string',
    storage = 'table'
}

session.new = function(opts)
    opts = opts or {}
    utils.check_param_table(opts, SESSION_NEW_OPTS)
    if opts.type ~= nil and opts.type ~= 'binary' then
        box.error(box.error.ILLEGAL_PARAMS,
                  "invalid session type '" .. opts.type .. "', " ..
                  "the only supported type is 'binary'")
    end
    local sid = box.iproto.internal.session_new(opts.fd, opts.user)
    -- It's okay to set the session storage after creating the session
    -- because session_new doesn't yield so no one could possibly access
    -- the uninitialized storage yet.
    assert(session.exists(sid))
    getmetatable(session).aggregate_storage[sid] = opts.storage
end
