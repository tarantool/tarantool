local tap = require('tap')

-- See also https://github.com/LuaJIT/LuaJIT/issues/463.
local test = tap.test('lj-463-os-date-oom')
test:plan(1)

-- Try all available locales to check the behaviour.
-- Before the patch, the call to `os.date('%p')` on non-standard
-- locale (ru_RU.utf8, sv_SE.utf8, etc.) may lead to OOM.
for locale in io.popen('locale -a'):read('*a'):gmatch('([^\n]*)\n?') do
    os.setlocale(locale)
    os.date('%p')
end

test:ok(true, 'os.date() finished without OOM')

test:done(true)
