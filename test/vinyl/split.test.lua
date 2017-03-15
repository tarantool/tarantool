test_run = require('test_run').new()
fiber = require('fiber')

space = box.schema.space.create("vinyl", { engine = 'vinyl' })
_= space:create_index('primary', { parts = { 1, 'unsigned' }, run_count_per_level = 1 })

function vyinfo() return space.index.primary:info() end

range_size = vyinfo().range_size
tuple_size = vyinfo().page_size / 2
tuples_per_range = math.ceil(range_size / tuple_size + 1)

test_run:cmd("setopt delimiter ';'")
function gen_tuple()
    local pad = {}
    for i = 1,tuple_size do
        pad[i] = string.char(math.random(65, 90))
    end
    return {math.random(4294967295), table.concat(pad)}
end;
for r=1,4 do
    for i=1,tuples_per_range do
        box.space.vinyl:replace(gen_tuple())
    end
    box.snapshot()
end;
test_run:cmd("setopt delimiter ''");

while vyinfo().range_count < 2 do fiber.sleep(0.1) end

vyinfo().range_count

for i=1,100 do box.space.vinyl:replace({i}) end

space:drop()
