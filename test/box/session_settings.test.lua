test_run = require('test_run').new()

-- User cannot create spaces with this engine.
s = box.schema.space.create('test', {engine = 'service'})

-- Check _session_settings space.
s = box.space._session_settings
s:format()

-- Make sure that we cannot drop space.
s:drop()

--
-- Make sure, that session_settings space doesn't support
-- create_index(), insert(), replace() and delete() methods.
--
s:create_index('a')
s:insert({'a', 1})
s:delete({'b'})
s:replace({'sql_defer_foreign_keys', true})

--
-- Check select() method of session_settings space. Should work
-- the same way as an ordinary space with an index of the type
-- "TREE".
--
s:select()

t = box.schema.space.create('settings', {format = s:format()})
_ = t:create_index('primary')
for _,value in s:pairs() do t:insert(value) end

test_run:cmd('setopt delimiter ";"')
function check_sorting(ss, ts, key)
    local iterators_list = {'ALL', 'REQ', 'EQ', 'GE', 'GT', 'LE', 'LT'}
    for _, it in pairs(iterators_list) do
        local view_space = ss:select({key}, {iterator = it})
        local test_space = ts:select({key}, {iterator = it})
        for key, value in pairs(view_space) do
            if test_space[key].name ~= value.name then
                return {
                    err = 'bad sorting', type = it,
                    exp = test_space[key].name, got = value.name
                }
            end
        end
    end
end;
test_run:cmd('setopt delimiter ""');

check_sorting(s, t)
check_sorting(s, t, 'abcde')
check_sorting(s, t, 'sql_d')
check_sorting(s, t, 'sql_v')
check_sorting(s, t, 'sql_defer_foreign_keys')

t:drop()

-- Check get() method of session_settings space.
s:get({'sql_defer_foreign_keys'})
s:get({'sql_recursive_triggers'})
s:get({'sql_reverse_unordered_selects'})
s:get({'sql_default_engine'})
s:get({'abcd'})

-- Check pairs() method of session_settings space.
t = {}
for key, value in s:pairs() do table.insert(t, {key, value}) end
#t == s:count()

-- Check update() method of session_settings space.

-- Correct updates.
s:update('sql_defer_foreign_keys', {{'=', 'value', true}})
s:update({'sql_defer_foreign_keys'}, {{'=', 2, false}})
s:update('sql_default_engine', {{'=', 2, 'vinyl'}})
s:update('sql_default_engine', {{':', 'value', 1, 5, 'memtx'}})
s:update('a', {{'=', 2, 1}})

-- Inorrect updates.
s:update({{'sql_defer_foreign_keys'}}, {{'=', 'value', true}})

s:update('sql_defer_foreign_keys', {'=', 'value', true})
s:update('sql_defer_foreign_keys', {{'=', 'value', true}, {'=', 2, true}})
s:update('sql_defer_foreign_keys', {{}})
s:update('sql_defer_foreign_keys', {{'='}})
s:update('sql_defer_foreign_keys', {{'=', 'value'}})
s:update('sql_defer_foreign_keys', {{'=', 'value', true, 1}})

s:update('sql_defer_foreign_keys', {{'+', 'value', 2}})
s:update('sql_defer_foreign_keys', {{'-', 'value', 2}})
s:update('sql_defer_foreign_keys', {{'&', 'value', 2}})
s:update('sql_defer_foreign_keys', {{'|', 'value', 2}})
s:update('sql_defer_foreign_keys', {{'^', 'value', 2}})
s:update('sql_defer_foreign_keys', {{'!', 'value', 2}})
s:update('sql_defer_foreign_keys', {{'#', 'value', 2}})
s:update('sql_defer_foreign_keys', {{1, 'value', true}})
s:update('sql_defer_foreign_keys', {{{1}, 'value', true}})

s:update('sql_defer_foreign_keys', {{'=', {'value'}, true}})
s:update('sql_defer_foreign_keys', {{'=', 1, 'new_key'}})
s:update('sql_defer_foreign_keys', {{'=', 'name', 'new_key'}})
s:update('sql_defer_foreign_keys', {{'=', 3, true}})
s:update('sql_defer_foreign_keys', {{'=', 'some text', true}})

s:update('sql_defer_foreign_keys', {{'=', 'value', 1}})
s:update('sql_defer_foreign_keys', {{'=', 'value', {1}}})
s:update('sql_defer_foreign_keys', {{'=', 'value', '1'}})

-- gh-4711: Provide a user-friendly frontend for accessing session settings.
settings = box.session.settings
assert(settings ~= nil)

s:update('sql_default_engine', {{'=', 2, 'vinyl'}})
settings.sql_default_engine
settings.sql_default_engine = 'memtx'
s:get('sql_default_engine').value
settings.sql_defer_foreign_keys = true
s:get('sql_defer_foreign_keys').value
s:update('sql_defer_foreign_keys', {{'=', 2, false}})
settings.sql_defer_foreign_keys

box.execute([[set session "sql_default_engine" = 'vinyl']])
s:get('sql_default_engine').value
box.execute([[set session "sql_default_engine" = 'memtx']])
s:get('sql_default_engine').value
box.execute([[set session "sql_defer_foreign_keys" = true]])
s:get('sql_defer_foreign_keys').value
box.execute([[set session "sql_defer_foreign_keys" = false]])
s:get('sql_defer_foreign_keys').value

settings.sql_default_engine = true
settings.sql_defer_foreign_keys = 'false'
settings.sql_parser_debug = 'string'

str = string.rep('a', 20 * 1024)
box.session.settings.sql_default_engine = str

box.execute([[set session "sql_def_engine" = true]])
box.execute([[set session "sql_default_engine" = true]])
box.execute([[set session "sql_defer_foreign_keys" = 'true']])
