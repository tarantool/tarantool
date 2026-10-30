local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

-- Below we use specially crafted tuple to work with mpstream. Inserting
-- `flush` helps to flush previously written data to the mpstream buffer
-- (region or ibuf or whatever).

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        -- Use vinyl engine to test Lua C implementation of get, min, max etc
        box.schema.space.create('test', {engine = 'vinyl'})
        box.space.test:create_index('pri')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.test_index_ops = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local bad = require('ffi').cast('void *', -1)
        local flush = string.rep('x', 1024 * 1024)
        local trouble = {1, flush, bad}
        local errmsg = "unsupported Lua type 'cdata'"
        local mem_used = function()
            return fiber.info()[fiber.self().id()].memory.used
        end
        local space = box.space.test
        local index = space.index.pri
        local before = mem_used()

        t.assert_error_msg_equals(errmsg, space.insert, space, trouble)
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, space.replace, space, trouble)
        t.assert_equals(before, mem_used())
        space:insert{1}
        t.assert_error_msg_equals(errmsg, space.update, space, trouble,
                                  {{'!', 2, 1}})
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, space.update, space, {1},
                                  {{'!', 2, trouble}})
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, space.upsert, space, trouble,
                                  {{'!', 2, 1}})
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, space.upsert, space, {1},
                                  {{'!', 2, trouble}})
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, index.get, index, trouble)
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, index.min, index, trouble)
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, index.max, index, trouble)
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, index.count, index, trouble)
        t.assert_equals(before, mem_used())

        -- test lbox_normalize_position
        t.assert_error_msg_equals(errmsg, space.select, space, {1},
                                  {after = trouble})
        t.assert_equals(before, mem_used())
    end)
end

-- Every time execute some RPC that can cause encoding failure and then run
-- some RPC that should success. This way we test that there were no garbage
-- left in the connection send buffer after failed RPC. In case of garbage
-- we expect errors.
g.test_netbox = function(cg)
    local bad = require('ffi').cast('void *', -1)
    local flush = string.rep('x', 1024 * 1024)
    local trouble = {1, flush, bad}
    local errmsg = "unsupported Lua type 'cdata'"
    local c = require('net.box').connect(cg.server.net_box_uri)
    local space = c.space.test

    -- group that tests luamp_convert_key on RPC encoding
    t.assert_error_msg_equals(errmsg, space.select, space, trouble)
    c:ping()
    t.assert_error_msg_equals(errmsg, space.delete, space, trouble)
    c:ping()
    t.assert_error_msg_equals(errmsg, space.update, space, trouble, {})
    c:ping()

    -- group that tests luamp_encode_tuple on RPC encoding
    t.assert_error_msg_equals(errmsg, c.call, c, 'f', trouble)
    c:ping()
    t.assert_error_msg_equals(errmsg, c.eval, c, 'f', trouble)
    c:ping()
    t.assert_error_msg_equals(errmsg, space.select, space, {1},
                              {after = trouble})
    c:ping()
    t.assert_error_msg_equals(errmsg, space.insert, space, trouble)
    c:ping()
    t.assert_error_msg_equals(errmsg, space.delete, space, trouble)
    c:ping()
    t.assert_error_msg_equals(errmsg, space.update, space, 1,
                              {{'!', 2, trouble}})
    c:ping()
    t.assert_error_msg_equals(errmsg, space.upsert, space, {1},
                              {{'!', 2, trouble}})
    c:ping()
    t.assert_error_msg_equals(errmsg, c.execute, c,
                              'SELECT * from TEST', trouble)
    c:ping()
end

-- Currently we don't have Lua API to check slab cache usage and we
-- can thus we cannot check it for leaks. At least check error paths work.
--
-- TODO(gh-8677) If in the scope of the above issue an assertion will be added
-- to the `cord_buf_on_yield` then we can test for leaks just by calling
-- fiber.yield.
g.test_misc_cord_ibuf = function(cg)
    cg.server:exec(function()
        local bad = require('ffi').cast('void *', -1)
        local flush = string.rep('x', 1024 * 1024)
        local trouble = {1, flush, bad}
        local errmsg = "unsupported Lua type 'cdata'"
        local msgpack = require('msgpack')

        -- This test does not work with cord ibuf. Let it be here though.
        local buffer = require('buffer')
        local buf = buffer.ibuf()
        msgpack.encode('something', buf)
        local before = buf:size()
        t.assert_error_msg_equals(errmsg, msgpack.encode, trouble, buf)
        t.assert_equals(buf:size(), before)

        t.assert_error_msg_equals(errmsg, msgpack.encode, trouble)
        t.assert_error_msg_equals(errmsg, msgpack.object, trouble)
        t.assert_error_msg_equals(errmsg, box.tuple.new, trouble)
        local tuple = box.tuple.new{1, 2, 3}
        t.assert_error_msg_equals(errmsg, box.tuple.transform,
                                  tuple, 2, 1, trouble)
        t.assert_error_msg_equals(errmsg, box.broadcast, 'key', trouble)
    end)
end

g.test_misc_region_leaks = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local bad = require('ffi').cast('void *', -1)
        local flush = string.rep('x', 1024 * 1024)
        local trouble = {1, flush, bad}
        local errmsg = "unsupported Lua type 'cdata'"
        local mem_used = function()
            return fiber.info()[fiber.self().id()].memory.used
        end

        local before = mem_used()
        t.assert_error_msg_equals(errmsg, box.iproto.send, box.session.id(),
                                  trouble, {})
        t.assert_equals(before, mem_used())
        t.assert_error_msg_equals(errmsg, box.iproto.send, box.session.id(),
                                  {}, trouble)
        t.assert_equals(before, mem_used())

        local ret, err = box.execute([[SELECT lua('
            return {
                1,
                string.rep("x", 1024 * 1024),
                require("ffi").cast("void *", -1)
            }
        ');]])
        t.assert_equals(ret, nil)
        t.assert_equals(tostring(err), errmsg)
        t.assert_equals(before, mem_used())

        ret, err = box.execute('SELECT ?;', {{trouble}})
        t.assert_equals(ret, nil)
        t.assert_equals(tostring(err), errmsg)
        t.assert_equals(before, mem_used())
    end)
end
