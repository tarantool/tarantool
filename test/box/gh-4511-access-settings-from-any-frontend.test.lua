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

-- Check get() and select(). They should return nothing for now.
s:get({'a'})
s:select()
s:select({}, {iterator='EQ'})
s:select({}, {iterator='ALL'})
s:select({}, {iterator='GE'})
s:select({}, {iterator='GT'})
s:select({}, {iterator='REQ'})
s:select({}, {iterator='LE'})
s:select({}, {iterator='LT'})
s:select({'a'}, {iterator='EQ'})
s:select({'a'}, {iterator='ALL'})
s:select({'a'}, {iterator='GE'})
s:select({'a'}, {iterator='GT'})
s:select({'a'}, {iterator='REQ'})
s:select({'a'}, {iterator='LE'})
s:select({'a'}, {iterator='LT'})

-- Currently there is nothing to update, but update() should work.
s:update('some_option', {{'=', 'value', true}})
