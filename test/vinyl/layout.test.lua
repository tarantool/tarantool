test_run = require('test_run').new()
test_run:cmd('restart server default with cleanup=1')

fiber = require 'fiber'
fio = require 'fio'
xlog = require 'xlog'
fun = require 'fun'

space = box.schema.space.create('test', {engine='vinyl'})
_ = space:create_index('pk', {parts = {{1, 'string', collation = 'unicode'}}, run_count_per_level=3})
_ = space:create_index('sk', {parts = {{2, 'unsigned'}}, run_count_per_level=3})

-- Empty run
space:insert{'ЁЁЁ', 777}
space:delete{'ЁЁЁ'}
box.snapshot()

space.index.sk:alter{parts = {{2, 'unsigned', is_nullable = true}}}

space:replace{'ЭЭЭ', box.NULL}
space:replace{'эээ', box.NULL}
space:replace{'ёёё', box.NULL}
box.snapshot()

space:replace{'ёёё', 123}
space:replace{'ЮЮЮ', 456}
space:replace{'ююю', 789}
box.snapshot()
space:drop()

-- Get the list of files from the last checkpoint.
-- convert names to relative
-- work_dir = fio.cwd()
files = box.backup.start()
-- use abspath to work correclty with symlinks
-- for i, name in pairs(files) do files[i] = fio.abspath(files[i]):sub(#work_dir + 2) end
table.sort(files)
-- files
result = {}
test_run:cmd("setopt delimiter ';'")
for i, path in pairs(files) do
    local suffix = string.gsub(path, '.*%.', '')
    if suffix ~= 'snap' and suffix ~= 'xlog' then
        local rows = {}
        local i = 1
        for lsn, row in xlog.pairs(path) do
            if row.BODY.bloom_filter ~= nil then
                row.BODY.bloom_filter = '<bloom_filter>'
            end
            rows[i] = row
            i = i + 1
        end
        table.insert(result, { fio.basename(path), rows })
    end
end;

test_run:cmd("setopt delimiter ''");

box.backup.stop() -- resume the garbage collection process

test_run:cmd("push filter 'timestamp: .*' to 'timestamp: <timestamp>'")
test_run:cmd("push filter 'offset: .*' to 'offset: <offset>'")
result
test_run:cmd("clear filter")
