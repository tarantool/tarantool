local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'default'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('primary')
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_local = function(cg)
    cg.server:exec(function()
        local function stat()
            local s = box.stat()
            return {
                begin = s.BEGIN.total,
                commit = s.COMMIT.total,
                rollback = s.ROLLBACK.total,
            }
        end
        box.stat.reset()
        t.assert_covers(stat(), {begin = 0, commit = 0, rollback = 0})
        box.space.test:insert({1})
        t.assert_covers(stat(), {begin = 1, commit = 1, rollback = 0})
        box.begin()
        box.space.test:insert({2})
        box.space.test:insert({3})
        t.assert_covers(stat(), {begin = 2, commit = 1, rollback = 0})
        box.commit()
        t.assert_covers(stat(), {begin = 2, commit = 2, rollback = 0})
        box.begin()
        box.space.test:insert({4})
        t.assert_covers(stat(), {begin = 3, commit = 2, rollback = 0})
        box.rollback()
        t.assert_covers(stat(), {begin = 3, commit = 2, rollback = 1})
    end)
    cg.server:restart()
    cg.server:exec(function()
        local function stat()
            local s = box.stat()
            return {
                begin = s.BEGIN.total,
                commit = s.COMMIT.total,
                rollback = s.ROLLBACK.total,
            }
        end
        t.assert_covers(stat(), {begin = 0, commit = 0, rollback = 0})
    end)
end

g.before_test('test_replication', function(cg)
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = cg.server.net_box_uri,
        },
    })
end)

g.after_test('test_replication', function(cg)
    cg.replica:drop()
end)

g.test_replication = function(cg)
    cg.server:exec(function()
        box.space.test:insert({1})
        box.snapshot()
        box.space.test:insert({2})
    end)
    cg.replica:start()
    cg.replica:exec(function()
        local function stat()
            local s = box.stat()
            return {
                begin = s.BEGIN.total,
                commit = s.COMMIT.total,
                rollback = s.ROLLBACK.total,
            }
        end
        t.assert_covers(stat(), {begin = 0, commit = 0, rollback = 0})
    end)
    local vclock = cg.server:exec(function()
        box.begin()
        box.space.test:insert{3}
        box.space.test:insert{4}
        box.commit()
        box.space.test:insert{5}
        box.begin()
        box.space.test:insert{6}
        box.space.test:insert{7}
        box.rollback()
        return box.info.vclock
    end)
    vclock[0] = nil
    cg.replica:wait_for_vclock(vclock)
    cg.replica:exec(function()
        local function stat()
            local s = box.stat()
            return {
                begin = s.BEGIN.total,
                commit = s.COMMIT.total,
                rollback = s.ROLLBACK.total,
            }
        end
        t.assert_covers(stat(), {begin = 2, commit = 2, rollback = 0})
    end)
end

g.after_test('test_wal_error', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
    end)
end)

g.test_wal_error = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local function stat()
            local s = box.stat()
            return {
                begin = s.BEGIN.total,
                commit = s.COMMIT.total,
                rollback = s.ROLLBACK.total,
            }
        end
        box.stat.reset()
        t.assert_covers(stat(), {begin = 0, commit = 0, rollback = 0})
        box.begin()
        t.assert_covers(stat(), {begin = 1, commit = 0, rollback = 0})
        box.space.test:insert{1}
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        t.assert_error_msg_equals("Failed to write to disk", box.commit)
        t.assert_covers(stat(), {begin = 1, commit = 0, rollback = 1})
    end)
end
