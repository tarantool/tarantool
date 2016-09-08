test_run = require('test_run').new()
fiber = require('fiber')

space = box.schema.space.create("vinyl", { engine = 'vinyl' })
_= space:create_index('primary', { parts = { 1, 'unsigned' } })

function vyinfo() return box.info.vinyl().db[box.space.vinyl.id..'/0'] end

range_size = vyinfo().range_size
tuple_size = 100 * 1024
tuples_per_range = math.ceil(range_size / tuple_size + 1)

test_run:cmd("setopt delimiter ';'")
local BUF = string.rep('x', 100 * 1024)
for r=1,2 do
    for i=1,tuples_per_range do
        box.space.vinyl:replace({math.random(4294967295), BUF})
    end
    box.snapshot()
end;
test_run:cmd("setopt delimiter ''");

while vyinfo().range_count < 2 do fiber.sleep(0.1) end

vyinfo().range_count

for i=1,100 do box.space.vinyl:replace({i}) end

space:drop()

fiber = nil
test_run = nil
