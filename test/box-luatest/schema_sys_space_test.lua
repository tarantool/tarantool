local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all = function(lg)
    lg.server = server:new({alias = 'master'})
    lg.server:start()
end

g.after_all = function(lg)
    lg.server:drop()
end

g.test_cluster_uuid_update_ban = function(lg)
    lg.server:exec(function()
        local uuid = require('uuid')
        local _schema = box.space._schema
        local msg = "Can't reset replica set UUID"
        t.assert_error_msg_contains(msg, _schema.replace, _schema,
                                    {'cluster', uuid.str()})
        t.assert_error_msg_contains(msg, _schema.replace, _schema,
                                    {'cluster', tostring(uuid.NULL)})
        -- Fine to replace with the same value.
        _schema:replace{'cluster', box.info.cluster.uuid}
    end)
end

g.test_version_transactional = function(lg)
    lg.server:exec(function()
        local ffi = require('ffi')
        ffi.cdef('uint32_t box_dd_version_id(void);')
        local dd_version = ffi.C.box_dd_version_id()

        box.begin()
        box.space._schema:update({'version'}, {{'+', 'value', 1}})
        local dd_version_in_tx = ffi.C.box_dd_version_id()
        box.rollback()

        t.assert_equals(dd_version, dd_version_in_tx)
        t.assert_equals(dd_version, ffi.C.box_dd_version_id())
    end)
end
