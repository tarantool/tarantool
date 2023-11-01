local server = require('luatest.server')
local t = require('luatest')
local g = t.group('gh-5717', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    -- Force disable MVCC to abort the transaction by fiber yield.
    local box_cfg = {memtx_use_mvcc_engine = false}
    cg.server = server:new{box_cfg = box_cfg}
    cg.server:start()
    cg.server:exec(function()
        -- Allows to inject an error into the trigger.
        rawset(_G, 'errinj_trigger_name', '')
        -- Set a trigger that will save all passed information into `_G.result'.
        local function trigger_set(event, event_full, type, name)
            local trigger = require('trigger')
            trigger.set(event_full, name, function(iterator)
                if name == _G.errinj_trigger_name then
                    error('errinj in ' .. name)
                end
                local tuples = {}
                for num, old_tuple, new_tuple, space_id in iterator() do
                    table.insert(tuples, {
                        num = num, old_tuple = old_tuple,
                        new_tuple = new_tuple, space_id = space_id
                    })
                end
                table.insert(_G.result, {event = event, type = type,
                                         name = name, tuples = tuples})
            end)
        end
        -- Set a global trigger on `event'.
        rawset(_G, 'trigger_set_global', function(event, name)
            trigger_set(event, event, 'global', name)
        end)
        -- Set a trigger on `event', filtered by `space_id'.
        rawset(_G, 'trigger_set_by_id', function(event, name, space_id)
            local event_full = string.format('%s.space[%d]', event, space_id)
            trigger_set(event, event_full, 'by_id', name)
        end)
        -- Set a trigger on `event', filtered by `space_name'.
        rawset(_G, 'trigger_set_by_name', function(event, name, space_name)
            local event_full = string.format('%s.space.%s', event, space_name)
            trigger_set(event, event_full, 'by_name', name)
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        -- Delete all registered triggers.
        local trigger = require('trigger')
        local trigger_info = trigger.info()
        for event, trigger_list in pairs(trigger_info) do
            for _, trigger_descr in pairs(trigger_list) do
                local trigger_name = trigger_descr[1]
                trigger.del(event, trigger_name)
            end
        end
        if box.space.bands then box.space.bands:drop() end
        if box.space.albums then box.space.albums:drop() end
    end)
end)

-- Test basic functionality of `before_commit', `on_commit' and `on_rollback'
-- triggers.
g.test_triggers_basic = function(cg)
    cg.server:exec(function(engine)
        local trigger = require('trigger')

        local opts = {engine = engine, id = 743}
        local s = box.schema.space.create('bands', opts)
        s:create_index('pk')
        _G.trigger_set_global('box.before_commit', 't1')
        _G.trigger_set_by_id('box.on_commit', 't2', s.id)
        _G.trigger_set_by_name('box.on_rollback', 't3', s.name)

        -- Check that triggers are called for a single-statement transaction.
        rawset(_G, 'result', {})
        s:auto_increment{'The Beatles', 1960}
        t.assert_equals(_G.result, {
            {event = 'box.before_commit', type = 'global', name = 't1',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {1, 'The Beatles', 1960}},
            }},
            {event = 'box.on_commit', type = 'by_id', name = 't2',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {1, 'The Beatles', 1960}},
            }},
        })

        -- Check that triggers are called for a multi-statement transaction.
        _G.result = {}
        box.begin()
        s:auto_increment{'The Rolling Stones', 1962}
        s:auto_increment{'Pink Floyd', 1965}
        s:delete{100500}
        box.commit()
        t.assert_equals(_G.result, {
            {event = 'box.before_commit', type = 'global', name = 't1',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {2, 'The Rolling Stones', 1962}},
                {num = 2, space_id = 743, old_tuple = nil,
                 new_tuple = {3, 'Pink Floyd', 1965}},
                {num = 3, space_id = 743, old_tuple = nil, new_tuple = nil},
            }},
            {event = 'box.on_commit', type = 'by_id', name = 't2',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {2, 'The Rolling Stones', 1962}},
                {num = 2, space_id = 743, old_tuple = nil,
                 new_tuple = {3, 'Pink Floyd', 1965}},
                {num = 3, space_id = 743, old_tuple = nil, new_tuple = nil},
            }},
        })

        -- Check `on_rollback' trigger. Note that `before_commit' is not called
        -- for explicit rollback.
        _G.result = {}
        box.begin()
        s:auto_increment{'The Stooges', 1967}
        s:auto_increment{'Black Sabbath', 1968}
        s:delete{100500}
        box.rollback()
        t.assert_equals(_G.result, {
            {event = 'box.on_rollback', type = 'by_name', name = 't3',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil, new_tuple = nil},
                {num = 2, space_id = 743, old_tuple = nil,
                 new_tuple = {5, 'Black Sabbath', 1968}},
                {num = 3, space_id = 743, old_tuple = nil,
                 new_tuple = {4, 'The Stooges', 1967}},
            }},
        })

        -- Check triggers on rollback to a savepoint.
        _G.result = {}
        box.begin()
        s:auto_increment{'Queen', 1970}
        local svp = box.savepoint()
        s:auto_increment{'The Dictators', 1973}
        s:auto_increment{'Automobil', 1974}
        box.rollback_to_savepoint(svp)
        s:auto_increment{'Ramones', 1974}
        box.commit()
        t.assert_equals(_G.result, {
            {event = 'box.on_rollback', type = 'by_name', name = 't3',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {6, 'Automobil', 1974}},
                {num = 2, space_id = 743, old_tuple = nil,
                 new_tuple = {5, 'The Dictators', 1973}},
            }},
            {event = 'box.before_commit', type = 'global', name = 't1',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {4, 'Queen', 1970}},
                {num = 2, space_id = 743, old_tuple = nil,
                 new_tuple = {5, 'Ramones', 1974}},
            }},
            {event = 'box.on_commit', type = 'by_id', name = 't2',
             tuples = {
                {num = 1, space_id = 743, old_tuple = nil,
                 new_tuple = {4, 'Queen', 1970}},
                {num = 2, space_id = 743, old_tuple = nil,
                 new_tuple = {5, 'Ramones', 1974}},
            }},
        })

        -- Check that only `before_commit' is called for an empty committed txn.
        _G.result = {}
        box.begin()
        box.commit()
        t.assert_equals(_G.result, {
            {event = 'box.before_commit', type = 'global', name = 't1',
             tuples = {}},
        })

        -- Check that no triggers are called for an empty rolled back txn.
        _G.result = {}
        box.begin()
        box.rollback()
        t.assert_equals(_G.result, {})

        -- Check that it is possible to delete triggers within an active
        -- transaction.
        _G.result = {}
        box.begin()
        s:auto_increment{'Smokie', 1964}
        trigger.del('box.before_commit', 't1')
        trigger.del('box.on_commit.space[743]', 't2')
        box.commit()
        t.assert_equals(_G.result, {})
    end, {cg.params.engine})
end

-- Tests with error injection to force rollback due to WAL failure.
g.test_triggers_debug = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local s = box.schema.space.create('bands', {engine = engine})
        s:create_index('pk')
        _G.trigger_set_global('box.before_commit', 'before')
        _G.trigger_set_global('box.on_commit', 'commit')
        _G.trigger_set_global('box.on_rollback', 'rollback')

        -- Check triggers on rollback by a WAL I/O error.
        -- Note the reverse order of tuples in `on_rollback' vs `before_commit'.
        rawset(_G, 'result', {})
        box.begin()
        s:auto_increment{'Король и Шут', 1988}
        s:auto_increment{'Б.А.У.', 2011}
        box.error.injection.set("ERRINJ_WAL_IO", true)
        t.assert_error_msg_equals('Failed to write to disk', box.commit)
        box.error.injection.set("ERRINJ_WAL_IO", false)
        t.assert_equals(_G.result, {
            {event = 'box.before_commit', type = 'global', name = 'before',
             tuples = {
                {num = 1, space_id = 512, old_tuple = nil,
                 new_tuple = {1, 'Король и Шут', 1988}},
                {num = 2, space_id = 512, old_tuple = nil,
                 new_tuple = {2, 'Б.А.У.', 2011}},
            }},
            {event = 'box.on_rollback', type = 'global', name = 'rollback',
             tuples = {
                {num = 1, space_id = 512, old_tuple = nil,
                 new_tuple = {2, 'Б.А.У.', 2011}},
                {num = 2, space_id = 512, old_tuple = nil,
                 new_tuple = {1, 'Король и Шут', 1988}},
            }},
        })
    end, {cg.params.engine})
end

-- Test the order of multiple triggers for one event.
g.test_triggers_order = function(cg)
    cg.server:exec(function(engine)
        local trigger = require('trigger')
        local opts = {engine = engine, id = 666}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk')

        -- Set two triggers of each type in random order, however trigger #2
        -- always follows trigger #1 of the same type.
        _G.trigger_set_global('box.before_commit', 'global 1')
        _G.trigger_set_by_id('box.on_commit', 'by_id 1', s.id)
        _G.trigger_set_by_name('box.on_commit', 'by_name 1', 'bands')
        _G.trigger_set_global('box.on_rollback', 'global 1')
        _G.trigger_set_by_name('box.before_commit', 'by_name 1', 'bands')
        _G.trigger_set_global('box.on_rollback', 'global 2')
        _G.trigger_set_by_id('box.before_commit', 'by_id 1', s.id)
        _G.trigger_set_global('box.before_commit', 'global 2')
        _G.trigger_set_by_id('box.on_rollback', 'by_id 1', s.id)
        _G.trigger_set_global('box.on_commit', 'global 1')
        _G.trigger_set_by_id('box.on_commit', 'by_id 2', s.id)
        _G.trigger_set_by_id('box.before_commit', 'by_id 2', s.id)
        _G.trigger_set_by_name('box.before_commit', 'by_name 2', 'bands')
        _G.trigger_set_by_id('box.on_rollback', 'by_id 2', s.id)
        _G.trigger_set_by_name('box.on_commit', 'by_name 2', 'bands')
        _G.trigger_set_global('box.on_commit', 'global 2')
        _G.trigger_set_by_name('box.on_rollback', 'by_name 1', 'bands')
        _G.trigger_set_by_name('box.on_rollback', 'by_name 2', 'bands')

        -- Check that `before_commit' and `on_commit' triggers of one type are
        -- executed in reverse order of their installation.
        -- Global triggers are always executed after space-specific triggers.
        rawset(_G, 'result', {})
        local tuple = s:auto_increment{'Skepticism', 1991}
        local tuples = {{num = 1, space_id = s.id, old_tuple = nil,
                         new_tuple = tuple}}
        t.assert_equals(_G.result, {
            {event = 'box.before_commit', type = 'by_id', name = 'by_id 2',
             tuples = tuples},
            {event = 'box.before_commit', type = 'by_id', name = 'by_id 1',
             tuples = tuples},
            {event = 'box.before_commit', type = 'global', name = 'global 2',
             tuples = tuples},
            {event = 'box.before_commit', type = 'global', name = 'global 1',
             tuples = tuples},
            {event = 'box.on_commit', type = 'by_id', name = 'by_id 2',
             tuples = tuples},
            {event = 'box.on_commit', type = 'by_id', name = 'by_id 1',
             tuples = tuples},
            {event = 'box.on_commit', type = 'global', name = 'global 2',
             tuples = tuples},
            {event = 'box.on_commit', type = 'global', name = 'global 1',
             tuples = tuples},
        })

        -- Note that there were no "by_name" events, because the triggers were
        -- set to the "bands" name, but the space is called "test".
        s:rename('bands')

        -- Check that `on_rollback' triggers are executed in reverse order of
        -- their installation.
        -- Global triggers are always executed after space-specific triggers.
        _G.result = {}
        box.begin()
        local tuple = s:auto_increment{'Apocalyptica', 1993}
        box.rollback()
        local tuples = {{num = 1, space_id = s.id, old_tuple = nil,
                         new_tuple = tuple}}
        t.assert_equals(_G.result, {
            {event = 'box.on_rollback', type = 'by_id', name = 'by_id 2',
             tuples = tuples},
            {event = 'box.on_rollback', type = 'by_id', name = 'by_id 1',
             tuples = tuples},
            {event = 'box.on_rollback', type = 'by_name', name = 'by_name 2',
             tuples = tuples},
            {event = 'box.on_rollback', type = 'by_name', name = 'by_name 1',
             tuples = tuples},
            {event = 'box.on_rollback', type = 'global', name = 'global 2',
             tuples = tuples},
            {event = 'box.on_rollback', type = 'global', name = 'global 1',
             tuples = tuples},
        })

        -- Delete all `before_commit' triggers.
        for event, trigger_list in pairs(trigger.info()) do
            if event:startswith('box.before_commit') then
                for _, trigger_descr in pairs(trigger_list) do
                    trigger.del(event, trigger_descr[1])
                end
            end
        end

        -- Check that if a trigger fails, the remaining triggers for this event
        -- (including global ones) are not called.
        _G.result = {}
        _G.errinj_trigger_name = 'by_name 2'
        local tuple = s:auto_increment{'U2', 1976}
        _G.errinj_trigger_name = ''
        local tuples = {{num = 1, space_id = s.id, old_tuple = nil,
                         new_tuple = tuple}}
        t.assert_equals(_G.result, {
            {event = 'box.on_commit', type = 'by_id', name = 'by_id 2',
             tuples = tuples},
            {event = 'box.on_commit', type = 'by_id', name = 'by_id 1',
             tuples = tuples},
        })
    end, {cg.params.engine})
end

-- Test cases with multiple spaces with triggers.
g.test_triggers_multi_space = function(cg)
    cg.server:exec(function(engine)
        local trigger = require('trigger')

        local bands = box.schema.space.create('bands', {engine = engine})
        bands:format({{name='id', type='unsigned'}})
        bands:create_index('pk')
        _G.trigger_set_by_name('box.on_commit', 'bands_commit', bands.name)
        _G.trigger_set_by_name('box.on_rollback', 'bands_rollb', bands.name)

        local albums = box.schema.space.create('albums', {engine = engine})
        albums:create_index('pk')
        _G.trigger_set_by_id('box.on_commit', 'albums_commit', albums.id)
        _G.trigger_set_by_id('box.on_rollback', 'albums_rollb', albums.id)

        -- Check that the trigger-function argument iterates only over the
        -- statements with given space name/id.
        rawset(_G, 'result', {})
        box.begin()
        local kraftwerk = bands:auto_increment{'Kraftwerk', 1970}
        albums:auto_increment{kraftwerk.id, 'Autobahn', 1974}
        albums:auto_increment{kraftwerk.id, 'Computerwelt', 1981}
        local rammstein = bands:auto_increment{'Rammstein', 1994}
        albums:auto_increment{rammstein.id, 'Herzeleid', 1995}
        albums:auto_increment{rammstein.id, 'Sehnsucht', 1997}
        box.commit()
        t.assert_equals(_G.result, {
            {event = 'box.on_commit', type = 'by_name', name = 'bands_commit',
             tuples = {
                {num = 1, space_id = 512, old_tuple = nil,
                 new_tuple = {1, 'Kraftwerk', 1970}},
                {num = 2, space_id = 512, old_tuple = nil,
                 new_tuple = {2, 'Rammstein', 1994}},
            }},
            {event = 'box.on_commit', type = 'by_id', name = 'albums_commit',
             tuples = {
                {num = 1, space_id = 513, old_tuple = nil,
                 new_tuple = {1, 1, 'Autobahn', 1974}},
                {num = 2, space_id = 513, old_tuple = nil,
                 new_tuple = {2, 1, 'Computerwelt', 1981}},
                {num = 3, space_id = 513, old_tuple = nil,
                 new_tuple = {3, 2, 'Herzeleid', 1995}},
                {num = 4, space_id = 513, old_tuple = nil,
                 new_tuple = {4, 2, 'Sehnsucht', 1997}},
            }},
        })

        -- Check triggers on rollback to a savepoint.
        _G.result = {}
        box.begin()
        local scorpions = bands:auto_increment{'Scorpions', 1965}
        albums:auto_increment{scorpions.id, 'Love at First Sting', 1984}
        local svp = box.savepoint()
        albums:auto_increment{scorpions.id, 'Crazy World', 1990}
        box.rollback_to_savepoint(svp)
        box.commit()
        t.assert_equals(_G.result, {
            {event = 'box.on_rollback', type = 'by_id', name = 'albums_rollb',
             tuples = {
                {num = 1, space_id = 513, old_tuple = nil,
                 new_tuple = {6, 3, 'Crazy World', 1990}},
            }},
            {event = 'box.on_commit', type = 'by_id', name = 'albums_commit',
             tuples = {
                {num = 1, space_id = 513, old_tuple = nil,
                 new_tuple = {5, 3, 'Love at First Sting', 1984}},
            }},
            {event = 'box.on_commit', type = 'by_name', name = 'bands_commit',
             tuples = {
                {num = 1, space_id = 512, old_tuple = nil,
                 new_tuple = {3, 'Scorpions', 1965}},
            }},
        })

        -- Check that it is possible to delete a trigger within an active
        -- transaction.
        _G.result = {}
        box.begin()
        local madsin = bands:auto_increment{'Mad Sin', 1987}
        albums:auto_increment{madsin.id, 'Break the Rules', 1992}
        trigger.del('box.on_commit.space.bands', 'bands_commit')
        box.commit()
        t.assert_equals(_G.result, {
            {event = 'box.on_commit', type = 'by_id', name = 'albums_commit',
             tuples = {
                {num = 1, space_id = 513, old_tuple = nil,
                 new_tuple = {6, 4, 'Break the Rules', 1992}},
            }},
        })
    end, {cg.params.engine})
end

-- Various `before_commit'-specific tests.
g.test_before_commit = function(cg)
    cg.server:exec(function(engine)
        local trigger = require('trigger')
        local bands = box.schema.space.create('bands', {engine = engine})
        local albums = box.schema.space.create('albums', {engine = engine})
        bands:format({{name='id', type='unsigned'},
                      {name='name', type='string'},
                      {name='year', type='unsigned'}})
        bands:create_index('pk')
        albums:create_index('pk')
        _G.trigger_set_global('box.on_commit', 'commit')

        trigger.set('box.before_commit.space.bands', 't', function(iterator)
            for _, _, band in iterator() do
                albums:auto_increment{band.id, 'The First Album', band.year}
            end
            return 'ignored'
        end)

        -- Check that it is possible to write into spaces from `before_commit'.
        rawset(_G, 'result', {})
        bands:auto_increment{'Motörhead', 1975}
        t.assert_equals(_G.result, {
            {event = 'box.on_commit', type = 'global', name = 'commit',
             tuples = {
                {num = 1, space_id = 512, old_tuple = nil,
                 new_tuple = {1, 'Motörhead', 1975}},
                {num = 2, space_id = 513, old_tuple = nil,
                 new_tuple = {1, 1, 'The First Album', 1975}},
            }},
        })

        -- Check that new entries do not survive the rollback.
        _G.result = {}
        box.begin()
        bands:auto_increment{'Madness', 1976}
        box.rollback()
        t.assert_equals(_G.result, {})
        trigger.del('box.before_commit.space.bands', 't')

        -- Check that if `before_commit' trigger yields on memtx without MVCC,
        -- the transaction is aborted.
        trigger.set('box.before_commit.space.bands', 't', function()
            require('fiber').yield()
        end)
        _G.result = {}
        box.begin()
        bands:auto_increment{'The Clash', 1976}
        pcall(box.commit)
        if engine == 'memtx' then
            t.assert_equals(_G.result, {})
        elseif engine == 'vinyl' then
            t.assert_equals(_G.result, {
                {event = 'box.on_commit', type = 'global', name = 'commit',
                 tuples = {
                    {num = 1, space_id = 512, old_tuple = nil,
                     new_tuple = {2, 'The Clash', 1976}},
                }},
            })
        end
        trigger.del('box.before_commit.space.bands', 't')

        -- Check that if a trigger raises an error, the transaction is aborted.
        trigger.set('box.before_commit.space.bands', 't', function()
            error('error in before_commit')
        end)
        _G.result = {}
        box.begin()
        local band = bands:auto_increment{'The Misfits', 1977}
        albums:auto_increment{band.id, 'Walk Among Us', 1982}
        t.assert_error_msg_content_equals('error in before_commit', box.commit)
        t.assert_equals(_G.result, {})
        trigger.del('box.before_commit.space.bands', 't')
    end, {cg.params.engine})
end

-- Various `on_commit' and `on_rollback'-specific tests.
g.test_commit_rollback = function(cg)
    cg.server:exec(function(engine)
        local trigger = require('trigger')
        local s = box.schema.space.create('bands', {engine = engine})
        s:create_index('pk')

        trigger.set('box.on_commit', 't', function()
            error('error in on_commit')
        end)
        trigger.set('box.on_rollback.space.bands', 't', function()
            box.error({reason = 'error in on_rollback', errcode = 42})
        end)

        -- Check that the error in the `on_commit' trigger is simply logged
        -- without raising.
        s:auto_increment{'Nena', 1981}

        -- Check that the error in the `on_rollback' trigger is simply logged
        -- without raising.
        box.begin()
        s:auto_increment{'The Cranberries', 1989}
        box.rollback()
    end, {cg.params.engine})

    t.helpers.retrying({}, function()
        t.assert_is_not(cg.server:grep_log('error in on_commit'), nil)
        t.assert_is_not(cg.server:grep_log('error in on_rollback'), nil)
    end)
end
