env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

uuid = require('uuid')
ffi = require('ffi')

-- Check uuid indices (gh-4268).
_ = box.schema.space.create('test', {engine=engine})
_ = box.space.test:create_index('pk', {parts={1,'uuid'}})

for i = 1,16 do\
    box.space.test:insert{uuid.new()}\
end

a = box.space.test:select{}
err = nil
for i = 1, #a - 1 do\
    if tostring(a[i][1]) >= tostring(a[i+1][1]) then\
        err = {a[i][1], a[i+1][1]}\
        break\
    end\
end

err

box.space.test:drop()
