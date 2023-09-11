local t = require('luatest')
local tarantool = require('tarantool')
local server = require('luatest.server')

-- If ASAN is enabled, the information about memory allocation is different.
-- There is no sense in testing it.
local function skip_if_asan_is_enabled()
    t.skip_if(tarantool.build.asan)
end

local function after_all(cg)
    cg.server:drop()
end

local function after_each(cg)
    cg.server:exec(function()
        if box.space.memtx then
            box.space.memtx:drop()
        end
        if box.space.vinyl then
            box.space.vinyl:drop()
        end
    end)
end

local g1 = t.group('gh-6762-tuple-info')

g1.before_all(function(cg)
    local box_cfg = {
        slab_alloc_factor = 1.3,
        slab_alloc_granularity = 32,
        memtx_max_tuple_size = 1200*1000
    }
    cg.server = server:new{box_cfg = box_cfg}
    cg.server:start()
end)
g1.after_all(after_all)
g1.after_each(after_each)

-- Test info() method of a runtime tuple.
g1.test_tuple_info_runtime = function(cg)
    cg.server:exec(function()
        -- Compact form of a tuple.
        t.assert_equals(box.tuple.new{string.rep('c', 252)}:info(),
                        { data_size = 255,
                          header_size = 6,
                          field_map_size = 0,
                          waste_size = 0,
                          arena = "runtime" }
        )
        -- Bulky form of a tuple.
        t.assert_equals(box.tuple.new{string.rep('b', 253)}:info(),
                        { data_size = 256,
                          header_size = 10,
                          field_map_size = 0,
                          waste_size = 0,
                          arena = "runtime" }
        )
    end)
end

-- Test info() method of a memtx tuple.
g1.test_tuple_info_memtx = function(cg)
    skip_if_asan_is_enabled()
    cg.server:exec(function()
        local s = box.schema.space.create('memtx')
        s:create_index('pk', {parts = {2}})

        -- Compact form of a tuple.
        t.assert_equals(s:insert{string.rep('c', 251), 0}:info(),
                        { data_size = 255,
                          header_size = 6,
                          field_map_size = 4,
                          waste_size = 247,
                          arena = "memtx" }
        )
        -- Bulky form of a tuple.
        t.assert_equals(s:insert{string.rep('b', 252), 1}:info(),
                        { data_size = 256,
                          header_size = 10,
                          field_map_size = 4,
                          waste_size = 242,
                          arena = "memtx" }
        )
        -- malloc'ed tuple.
        t.assert_equals(s:insert{string.rep('m', 1100*1000), 2}:info(),
                        { data_size = 1100007,
                          header_size = 10,
                          field_map_size = 4,
                          waste_size = 0,
                          arena = "malloc" }
        )
        -- Check that info().data_size equals bsize()
        t.assert_equals(s:get(0):info().data_size, s:get(0):bsize())
        t.assert_equals(s:get(1):info().data_size, s:get(1):bsize())
        t.assert_equals(s:get(2):info().data_size, s:get(2):bsize())
    end)
end

-- Test info() method of a vinyl tuple.
g1.test_tuple_info_vinyl = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vinyl', {engine = 'vinyl'})
        s:create_index('pk', {parts = {2}})
        t.assert_equals(s:insert{'v', 0}:info(),
                        { data_size = 4,
                          header_size = 24,
                          field_map_size = 4,
                          waste_size = 0,
                          arena = "malloc" }
        )
        -- Check that info().data_size equals bsize()
        t.assert_equals(s:get(0):info().data_size, s:get(0):bsize())
    end)
end

-- Test error messages.
g1.test_errors = function()
    t.assert_error_msg_content_equals(
        'Usage: tuple:info()',
        function() box.tuple.new{0}.info() end
    )
    t.assert_error_msg_content_equals(
        'Usage: tuple:info()',
        function() box.tuple.new{0}:info('xxx') end
    )
    t.assert_error_msg_equals(
        'Invalid argument #1 (box.tuple expected, got string)',
        box.tuple.new{0}.info, 'xxx'
    )
end
