local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('p')
        rawset(_G, 'payload',
               {string.rep('x', 1024 * 1024 * 10),
                setmetatable({}, {__serialize = function()
                    error('error')
                end})})
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_msgpackffi_encode_memory_leak_on_error_condition = function(cg)
    cg.server:exec(function()
        local msgpackffi = require('msgpackffi')

        local payload = _G.payload
        for _ = 1, 1000 do
            t.assert_error_msg_contains("error", function()
                msgpackffi.encode(payload)
            end)
        end
    end)
end

g.test_swim_set_payload_memory_leak_on_error_condition = function(cg)
    cg.server:exec(function()
        local swim = require('swim')
        local uuid = require('uuid')

        local payload = _G.payload
        local s = swim.new{uuid = uuid(1), uri = 3301}
        for _ = 1, 1000 do
            local _, err = s:set_payload(payload)
            t.assert_str_contains(tostring(err), "error")
        end
    end)
end

g.test_tuple_update_upsert_memory_leak_on_error_condition = function(cg)
    cg.server:exec(function()
        local err_msg = "error"
        local payload = _G.payload
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg,
                                  function()
                box.tuple.new():update(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg,
                                  function()
                box.tuple.new():update(payload)
            end)
        end
    end)
end

g.test_space_methods_memory_leak_on_error_condition = function(cg)
    cg.server:exec(function()
        local err_msg = "error"
        local payload = _G.payload
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s:pairs(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s:pairs(nil, {after = payload})
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s:select(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s:select(nil, {after = payload})
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s:get(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s:count(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s.index.p:min(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s.index.p:max(payload)
            end)
        end
        for _ = 1, 1000 do
            t.assert_error_msg_contains(err_msg, function()
                box.space.s.index.p:tuple_pos(payload)
            end)
        end
    end)
end
