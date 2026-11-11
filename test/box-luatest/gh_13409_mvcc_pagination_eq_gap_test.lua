local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13409', t.helpers.matrix({
    iterator = {'EQ', 'REQ'},
    secondary = {false, true},
    hint = {false, true},
    sort_order = {'asc', 'desc'},
}))

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(params)
        local s = box.schema.space.create('s')
        s:create_index('pk', {
            parts = {
                {1, 'unsigned', sort_order = params.sort_order},
                {2, 'unsigned', sort_order = params.sort_order},
            },
            hint = params.hint,
        })
        if params.secondary then
            s:create_index('sk', {
                parts = {{1, 'unsigned', sort_order = params.sort_order}},
                unique = false, hint = params.hint,
            })
        end
    end, {cg.params})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Key {4} matches tuples whose first field (the equality prefix) is 4.
-- Read two pages; 'after' resumes strictly past the last tuple of the first.
-- 'forward' means increasing values: EQ/asc or REQ/desc, not just ITER_EQ.
-- The tuples below are listed in scan order:
--                     first page       matching next  nonmatching next  insert
-- Increasing values:  {4, 3}, {4, 4}    {4, 6}         {5, 1}            {4, 5}
-- Decreasing values:  {4, 6}, {4, 5}    {4, 3}         {3, 1}            {4, 4}
-- 'boundary' chooses the next tuple; 'end' means neither next tuple exists.
local function check_gap(cg, boundary)
    cg.server:exec(function(params, boundary)
        local txn_proxy = require('test.box.lua.txn_proxy')
        local writer = txn_proxy.new()
        local s = box.space.s
        local index = params.secondary and s.index.sk or s.index.pk
        local forward = (params.iterator == 'EQ') ==
                        (params.sort_order == 'asc')
        local first = {4, forward and 3 or 6}
        local second = {4, forward and 4 or 5}
        local last = {4, forward and 6 or 3}
        s:insert(first)
        s:insert(second)
        if boundary == 'matching' then
            s:insert(last)
        elseif boundary == 'nonmatching' then
            s:insert({forward and 5 or 3, 1})
        end
        box.internal.memtx_tx_gc(100)

        box.begin()
        local tuples, pos = index:select({4}, {
            iterator = params.iterator, limit = 2, fetch_pos = true,
        })
        t.assert_equals(tuples, {first, second})
        t.assert_type(pos, 'string')
        local page = boundary == 'matching' and {last} or {}
        t.assert_equals(index:select({4}, {
            iterator = params.iterator, after = pos,
        }), page)

        -- Insert into the gap crossed at the start of the second page.
        local value = forward and 5 or 4
        t.assert_equals(writer('return box.space.s:insert{4, ' .. value .. '}'),
                        {{4, value}})
        writer:close()
        local expected = {first, second}
        if boundary == 'matching' then
            table.insert(expected, last)
        end
        t.assert_equals(index:select({4}, {iterator = params.iterator}),
                        expected)
        t.assert_error_covers({code = box.error.TRANSACTION_CONFLICT},
                              s.replace, s, {9, 9})
        box.rollback()
    end, {cg.params, boundary})
end

-- An insert between the resume position and the first tuple of the second
-- page must conflict, even though both pages use an equality iterator.
g.test_gap_before_first_tuple = function(cg)
    check_gap(cg, 'matching')
end

-- An empty page at the end of the index must detect a new matching tuple
-- beyond its resume position.
g.test_empty_page_at_end = function(cg)
    check_gap(cg, 'end')
end

-- An empty page ending at a different first-field value must still detect
-- inserts matching key {4} between the resume position and that boundary.
g.test_empty_page_before_nonmatching_tuple = function(cg)
    check_gap(cg, 'nonmatching')
end

-- Read an empty page for key {4} strictly past {4, 4}, without reading an
-- earlier page that could mask a missing pagination gap tracker.
-- Test both an existing/missing resume tuple and an index-end/different-key
-- boundary. 'writes' selects the concurrent changes:
-- * 'unrelated': insert {3, 1} and {5, 1}, outside the equality prefix {4}.
-- * 'excluded': insert before the resume position and at it if absent.
-- * 'rollback': insert inside the page and roll back, then insert other keys.
-- None of these changes should conflict. If 'conflict' is true, a final insert
-- inside the page must conflict: {4, 5} for increasing values,
-- {4, 3} otherwise.
local function check_independent_gap(cg, writes, conflict)
    for _, test_case in ipairs(t.helpers.matrix({
        after_present = {false, true},
        boundary = {'end', 'nonmatching'},
    })) do
        cg.server:exec(function(params, case, writes, conflict)
            local txn_proxy = require('test.box.lua.txn_proxy')
            local writer = txn_proxy.new()
            local s = box.space.s
            local index = params.secondary and s.index.sk or s.index.pk
            local forward = (params.iterator == 'EQ') ==
                            (params.sort_order == 'asc')
            local after = {4, 4}
            local opts = {iterator = params.iterator, after = after}
            if case.after_present then
                s:insert(after)
            end
            if case.boundary == 'nonmatching' then
                s:insert{forward and 6 or 2, 1}
            end
            box.internal.memtx_tx_gc(100)

            local function insert(tuple)
                local stmt = ('return box.space.s:insert{%d, %d}'):
                    format(tuple[1], tuple[2])
                t.assert_equals(writer(stmt), {tuple})
            end

            box.begin()
            -- No earlier page may install a tracker for the original prefix.
            t.assert_equals(index:select({4}, opts), {})
            if writes == 'rollback' then
                t.assert_equals(writer:begin(), '')
                insert({4, forward and 6 or 2})
                t.assert_equals(writer:rollback(), '')
            end
            if writes == 'excluded' then
                insert({4, forward and 3 or 5})
                if not case.after_present then
                    insert(after)
                end
            else
                insert({3, 1})
                insert({5, 1})
            end

            -- A repeat read could hide a lost tracker by creating a new one.
            t.assert_equals(s:replace{9, 9}, {9, 9})
            if conflict then
                insert({4, forward and 5 or 3})
                t.assert_error_covers({code = box.error.TRANSACTION_CONFLICT},
                                      box.commit)
            else
                box.commit()
                t.assert_equals(s:get{9, 9}, {9, 9})
            end
            writer:close()
            s:truncate()
        end, {cg.params, test_case, writes, conflict})
    end
end

-- Reading key {4} must not conflict with inserts whose first field is 3 or 5.
g.test_independent_page_unrelated_prefix = function(cg)
    check_independent_gap(cg, 'unrelated', false)
end

-- The resume position is exclusive: neither inserting {4, 4} itself nor a
-- tuple preceding it in scan order may conflict with the page read.
g.test_independent_page_exclusive_after = function(cg)
    check_independent_gap(cg, 'excluded', false)
end

-- Inserts of other keys may move the gap tracker. It must still detect a
-- subsequent insert matching key {4} beyond the resume position.
g.test_independent_page_tracker_relocation = function(cg)
    check_independent_gap(cg, 'unrelated', true)
end

-- An insert inside the page splits its gap tracker. After that insert rolls
-- back, inserts with first field 3 or 5 must still not conflict.
g.test_independent_page_split_rollback_unrelated_prefix = function(cg)
    check_independent_gap(cg, 'rollback', false)
end

-- Splitting the gap, rolling back, and inserting other keys must not lose the
-- tracker: a subsequent committed insert inside the page must conflict.
g.test_independent_page_split_rollback_retains_gap = function(cg)
    check_independent_gap(cg, 'rollback', true)
end

g.test_multi_part_prefix = function(cg)
    cg.server:exec(function(params)
        local txn_proxy = require('test.box.lua.txn_proxy')
        local writer = txn_proxy.new()
        local s = box.space.s
        if params.secondary then
            s.index.sk:drop()
        end
        s.index.pk:alter({parts = {
            {1, 'unsigned', sort_order = params.sort_order},
            {2, 'unsigned', sort_order = params.sort_order},
            {3, 'unsigned', sort_order = params.sort_order},
        }})
        if params.secondary then
            s:create_index('sk', {
                parts = {
                    {1, 'unsigned', sort_order = params.sort_order},
                    {2, 'unsigned', sort_order = params.sort_order},
                },
                unique = false, hint = params.hint,
            })
        end
        local index = params.secondary and s.index.sk or s.index.pk
        box.begin()
        t.assert_equals(index:select({4, 4}, {
            iterator = params.iterator, after = {4, 4, 4},
        }), {})
        t.assert_equals(writer('return box.space.s:insert{4, 3, 1}'),
                        {{4, 3, 1}})
        t.assert_equals(writer('return box.space.s:insert{4, 5, 1}'),
                        {{4, 5, 1}})
        t.assert_equals(s:replace{9, 9, 9}, {9, 9, 9})
        local forward = (params.iterator == 'EQ') ==
                        (params.sort_order == 'asc')
        local value = forward and 5 or 3
        t.assert_equals(
            writer('return box.space.s:insert{4, 4, ' .. value .. '}'),
            {{4, 4, value}})
        t.assert_error_covers({code = box.error.TRANSACTION_CONFLICT},
                             box.commit)
        writer:close()
    end, {cg.params})
end

-- Full pagination position must not cause a point tracker
-- to be created with a NULL key.
g.test_empty_key = function(cg)
    cg.server:exec(function(params)
        local s = box.space.s
        local index = params.secondary and s.index.sk or s.index.pk
        local opts = {iterator = params.iterator, after = {4, 4}}

        box.begin()
        t.assert_equals(index:select(nil, opts), {})
        t.assert_equals(index:select({}, opts), {})
        box.commit()

        s:insert{4, 4}
        box.begin()
        t.assert_equals(index:select(nil, opts), {})
        t.assert_equals(index:select({}, opts), {})
        box.commit()
    end, {cg.params})
end
