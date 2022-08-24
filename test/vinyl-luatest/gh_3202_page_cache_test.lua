-- https://github.com/tarantool/tarantool/issues/3202

local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {
            vinyl_cache = 0,
            vinyl_page_cache = 128 * 1024,
            vinyl_page_size = 1024,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        box.schema.space.create('test', {engine = 'vinyl'})
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:drop() end)
end)

--
-- Basic test set to check the page cache's ability to
-- actually cache data (request type: get)
--
g.test_page_cache_basic = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        local stat = nil
        local function stat_changed()
            local old_stat = stat
            local new_stat = box.space.test.index.pk:stat()
            stat = new_stat
            return (old_stat == nil or
                    old_stat.disk.iterator.read.pages ~=
                    new_stat.disk.iterator.read.pages)
        end

        local s = box.space.test
        s:create_index('pk')

        -- create multiple runs
        for i = 1,2500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 2501,5000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 5001,7500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 7501,10000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()

        box.begin()
        s:get{1} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{1} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{8001} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{8003} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{5123} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{50} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{5120} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{1} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{1} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{35} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{8030} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{43} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{5001} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{5003} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:get{4999} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:get{4997} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
    end)
end

--
-- Basic test set to check the page cache's ability to
-- actually cache data, but without beginning or ending
-- transactions (request type: get)
--
g.test_page_cache_basic = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        local stat = nil
        local function stat_changed()
            local old_stat = stat
            local new_stat = box.space.test.index.pk:stat()
            stat = new_stat
            return (old_stat == nil or
                    old_stat.disk.iterator.read.pages ~=
                    new_stat.disk.iterator.read.pages)
        end

        local s = box.space.test
        s:create_index('pk')

        -- create multiple runs
        for i = 1,2500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 2501,5000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 5001,7500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 7501,10000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()

        s:get{1} -- miss
        t.assert_is(stat_changed(), true)
        s:get{1} -- hit
        t.assert_is(stat_changed(), false)
        s:get{8001} -- miss
        t.assert_is(stat_changed(), true)
        s:get{8003} -- hit
        t.assert_is(stat_changed(), false)
        s:get{5123} -- miss
        t.assert_is(stat_changed(), true)
        s:get{50} -- miss
        t.assert_is(stat_changed(), true)
        s:get{5120} -- hit
        t.assert_is(stat_changed(), false)
        s:get{1} -- hit
        t.assert_is(stat_changed(), false)
        s:get{1} -- hit
        t.assert_is(stat_changed(), false)
        s:get{35} -- miss
        t.assert_is(stat_changed(), true)
        s:get{8030} -- miss
        t.assert_is(stat_changed(), true)
        s:get{43} -- hit
        t.assert_is(stat_changed(), false)
        s:get{5001} -- miss
        t.assert_is(stat_changed(), true)
        s:get{5003} -- hit
        t.assert_is(stat_changed(), false)
        s:get{4999} -- miss
        t.assert_is(stat_changed(), true)
        s:get{4997} -- hit
        t.assert_is(stat_changed(), false)
    end)
end

--
-- Basic test set to check the page cache's ability to
-- actually cache data (request type: select)
--
g.test_page_cache_basic = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        local stat = nil
        local function stat_changed()
            local old_stat = stat
            local new_stat = box.space.test.index.pk:stat()
            stat = new_stat
            return (old_stat == nil or
                    old_stat.disk.iterator.read.pages ~=
                    new_stat.disk.iterator.read.pages)
        end

        local s = box.space.test
        s:create_index('pk')

        -- create multiple runs
        for i = 1,2500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 2501,5000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 5001,7500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 7501,10000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()

        box.begin()
        s:select{1} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{1} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{8001} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{8003} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{5123} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{50} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{5120} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{1} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{1} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{35} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{8030} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{43} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{5001} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{5003} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
        box.begin()
        s:select{4999} -- miss
        box.commit()
        t.assert_is(stat_changed(), true)
        box.begin()
        s:select{4997} -- hit
        box.commit()
        t.assert_is(stat_changed(), false)
    end)
end

--
-- Basic test set to check the page cache's ability to
-- actually cache data, but without beginning or ending
-- transactions (request type: select)
--
g.test_page_cache_basic = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        local stat = nil
        local function stat_changed()
            local old_stat = stat
            local new_stat = box.space.test.index.pk:stat()
            stat = new_stat
            return (old_stat == nil or
                    old_stat.disk.iterator.read.pages ~=
                    new_stat.disk.iterator.read.pages)
        end

        local s = box.space.test
        s:create_index('pk')

        -- create multiple runs
        for i = 1,2500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 2501,5000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 5001,7500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 7501,10000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()

        s:select{1} -- miss
        t.assert_is(stat_changed(), true)
        s:select{1} -- hit
        t.assert_is(stat_changed(), false)
        s:select{8001} -- miss
        t.assert_is(stat_changed(), true)
        s:select{8003} -- hit
        t.assert_is(stat_changed(), false)
        s:select{5123} -- miss
        t.assert_is(stat_changed(), true)
        s:select{50} -- miss
        t.assert_is(stat_changed(), true)
        s:select{5120} -- hit
        t.assert_is(stat_changed(), false)
        s:select{1} -- hit
        t.assert_is(stat_changed(), false)
        s:select{1} -- hit
        t.assert_is(stat_changed(), false)
        s:select{35} -- miss
        t.assert_is(stat_changed(), true)
        s:select{8030} -- miss
        t.assert_is(stat_changed(), true)
        s:select{43} -- hit
        t.assert_is(stat_changed(), false)
        s:select{5001} -- miss
        t.assert_is(stat_changed(), true)
        s:select{5003} -- hit
        t.assert_is(stat_changed(), false)
        s:select{4999} -- miss
        t.assert_is(stat_changed(), true)
        s:select{4997} -- hit
        t.assert_is(stat_changed(), false)
    end)
end

--
-- Standard page cache memory cutback (upon memory quota being
-- reached) test
--
g.test_page_cache_mem_cutback = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        local s = box.space.test
        s:create_index('pk')

        -- create multiple runs
        for i = 1,2500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 2501,5000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 5001,7500 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()
        for i = 7501,10000 do s:replace{i, string.rep('x', 100)} end
        box.snapshot()

        t.assert_is(box.stat.vinyl().memory.page_cache, 0)
        -- when the quota is reached, LRU list should start rotating,
        -- and after that memory usage will remain the same
        -- quota must not be exceeded by more than 1 page's size
        for i = 1,10000,10 do
            s:get{i}
            t.assert_le(box.stat.vinyl().memory.page_cache, 128 * 1024 + 1624)
        end
        -- 1624 (> 1024, which is the formal size page in box.cfg) is
        -- an approximate maximum size of the real page in memory
    end)
end

--
-- Page cache memory quota variation test
--
g.test_page_cache_quota_variation = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        local s = box.space.test
        s:create_index('pk')

        -- create multiple runs
        for i = 1,2500,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 2501,5000,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 5001,7500,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 7501,10000,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 10001,12000,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 12001,14000,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 14001,16000,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()
        for i = 16001,20000,10 do s:replace{i, string.rep('x', 200)} end
        box.snapshot()

        t.assert_is(box.stat.vinyl().memory.page_cache, 0)

        for i = 1,18000,10 do s:get{i} end
        t.assert_ge(box.stat.vinyl().memory.page_cache, 100 * 1024)

        -- reduce the quota below current memory usage
        box.cfg{vinyl_page_cache = 64 * 1024}
        -- memory cutback must be triggered immediately
        t.assert_le(box.stat.vinyl().memory.page_cache, 64 * 1024)

        box.cfg{vinyl_page_cache = 86 * 1024}
        -- a few new pages in the cache to exceed the old quota
        for i = 18001,20000,200 do s:get{i} end
        -- memory usage should be above the old quota and below the new one
        t.assert_le(box.stat.vinyl().memory.page_cache, 86 * 1024)
        t.assert_ge(box.stat.vinyl().memory.page_cache, 64 * 1024)

        box.cfg{vinyl_page_cache = 0}
        -- cache must be emptied immediately
        t.assert_is(box.stat.vinyl().memory.page_cache, 0)

        -- no more than 1 page can be stored in memory at one time;
        -- in other words, quota can be exceeded by the approximate
        -- size of 1 page maximum
        for i = 5001,9000,200 do
            s:get{i}
            t.assert_le(box.stat.vinyl().memory.page_cache, 1624)
        end
        for i = 15, 4000, 12 do
            s:get{i}
            t.assert_le(box.stat.vinyl().memory.page_cache, 1624)
        end
        -- 1624 (> 1024, which is the formal size page in box.cfg) is
        -- an approximate maximum size of the real page in memory
    end)
end
