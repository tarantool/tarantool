local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')
local xlog = require('xlog')
local uuid = require('uuid')

local g = t.group()

g.before_all(function(cg)
    g.box_cfg = { instance_uuid = uuid() }
    cg.server = server:new({box_cfg = g.box_cfg})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_test('test_bad_xlog', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        box.snapshot()
    end)
end)

g.test_bad_xlog = function(cg)
    t.tarantool.skip_if_not_debug()
    local info = cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('primary', {parts = {1, 'string'}})
        box.snapshot()
        local lsn = box.info.lsn
        local function test(errinj)
            box.error.injection.set(errinj, true)
            local ok = pcall(s.insert, s, {errinj})
            box.error.injection.set(errinj, false)
            t.assert(ok)
        end
        box.space.test:insert({'BEGIN'})
        test('ERRINJ_XLOG_WRITE_CORRUPTED_HEADER')
        test('ERRINJ_XLOG_WRITE_INVALID_HEADER')
        test('ERRINJ_XLOG_WRITE_CORRUPTED_BODY')
        test('ERRINJ_XLOG_WRITE_INVALID_BODY')
        test('ERRINJ_XLOG_WRITE_INVALID_KEY')
        test('ERRINJ_XLOG_WRITE_INVALID_VALUE')
        test('ERRINJ_XLOG_WRITE_UNKNOWN_KEY')
        test('ERRINJ_XLOG_WRITE_UNKNOWN_TYPE')
        box.space.test:insert({'END'})
        return {
            replica_id = box.info.id,
            lsn = lsn,
            space_id = box.space.test.id
        }
    end)
    local path = fio.pathjoin(cg.server.workdir,
                              string.format('%020d.xlog', info.lsn))
    local result = {}
    for _, row in xlog.pairs(path) do
        if type(row.HEADER) == 'table' then
            if type(row.HEADER.timestamp) == 'number' then
                row.HEADER.timestamp = '<timestamp>'
            end
        end
        table.insert(result, row)
    end
    t.assert_equals(result, {
        {
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 1,
                timestamp = '<timestamp>',
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'BEGIN'},
            },
        },
        -- Row with a corrupted header is skipped.
        -- Row with an invalid header is skipped.
        -- Row with a corrupted body is skipped.
        -- Row with an invalid body is skipped.
        {
            -- Invalid keys are ignored.
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 6,
                timestamp = '<timestamp>',
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'ERRINJ_XLOG_WRITE_INVALID_KEY'},
            },
        },
        {
            -- Invalid values are dumped as is.
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 7,
                timestamp = '<timestamp>',
                key = 1,
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'ERRINJ_XLOG_WRITE_INVALID_VALUE'},
                key = 2,
            },
        },
        {
            -- Unknown keys are dumped as is.
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 8,
                timestamp = '<timestamp>',
                [666] = 1,
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'ERRINJ_XLOG_WRITE_UNKNOWN_KEY'},
                [666] = 2,
            },
        },
        {
            -- Unknown type is dumped as is.
            HEADER = {
                type = 777,
                replica_id = info.replica_id,
                lsn = info.lsn + 9,
                timestamp = '<timestamp>',
            },
            BODY = {
                [box.iproto.key.SPACE_ID] = info.space_id,
                [box.iproto.key.TUPLE] = {'ERRINJ_XLOG_WRITE_UNKNOWN_TYPE'},
            },
        },
        {
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 10,
                timestamp = '<timestamp>',
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'END'},
            },
        },
    })
end

g.test_xlog_meta = function(cg)
    local glob = fio.glob(fio.pathjoin(cg.server.workdir, '*.snap'))
    local snap_path = glob[#glob]

    local meta = xlog.meta(snap_path)
    t.assert_equals(meta.filetype, 'SNAP')
    t.assert_equals(meta.instance_uuid, cg.box_cfg.instance_uuid)
    t.assert_not_equals(meta.vclock, nil)
    t.assert_not_equals(meta.prev_vclock, nil)
end
