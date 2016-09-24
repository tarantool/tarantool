--
-- Check that ranges left from old dumps and incomplete splits
-- are ignored during initial recovery
--
test_run = require('test_run').new()
errinj = box.error.injection

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('primary')

test_run:cmd("setopt delimiter ';'")
function gen(i)
    local pad = string.rep('x', 16 + i)
    local n = (8 + i) * math.floor(box.cfg.vinyl.range_size / 16)
    for k = 1,n do
        s:replace{k, i + k, pad}
    end
    box.snapshot()
end;
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_VY_GC", true)
for i=1,4 do gen(i) end
errinj.set("ERRINJ_VY_RANGE_SPLIT", true)
for i=5,6 do gen(i) end

test_run:cmd('restart server default')

s = box.space.test

test_run:cmd("setopt delimiter ';'")
function check(i)
    local n = (8 + i) * math.floor(box.cfg.vinyl.range_size / 16)
    local n_corrupted = 0
    for k=1,n do
        local v = s:get(k)
        if not v or v[2] ~= i + k then
			n_corrupted = n_corrupted + 1
		end
    end
    return n - s:count(), n_corrupted
end;
test_run:cmd("setopt delimiter ''");

check(6)

s:drop()
