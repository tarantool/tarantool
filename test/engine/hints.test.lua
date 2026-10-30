--
-- gh-3961: Introduce tuple comparison hints
-- We must to enshure that hints don't broke
-- tuple comparison.
--
ffi = require('ffi')

test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

inspector:cmd("setopt delimiter ';'");
function insert_values(type)
        local x = 54
        while (x < 64) do
                local val = ffi.new(type, (2LLU^x)-1)
                s:replace({val})
                x = x + 1
        end
end;
inspector:cmd("setopt delimiter ''");

-- Test that hints does not violate the correct order of
-- big numeric fields.
s = box.schema.space.create('test', {engine = engine})
i1 = s:create_index('i1', {parts = {1, 'unsigned'}})
insert_values('uint64_t')
s:select()
i1:alter{parts = {1, 'integer'}}
insert_values('int64_t')
s:select()
i1:alter{parts = {1, 'number'}}
insert_values('double')
s:select()
s:drop()

-- Test that the use of hint(s) does not violate alter between
-- scalar and string.
s = box.schema.space.create('test', {engine = engine})
i1 = s:create_index('i1', {parts = {1, 'string'}})
s:insert({"bbb"})
s:insert({"ccc"})
i1:alter{parts = {1, 'scalar'}}
s:insert({"aaa"})
s:insert({"ddd"})
s:select()
s:drop()

-- Test that hints does not violate the correct order of
-- numeric fields (on alter).
s = box.schema.space.create('test', {engine = engine})
i1 = s:create_index('i1', {parts = {1, 'unsigned'}})
s:insert({11})
i1:alter{parts = {1, 'integer'}}
s:insert({-22})
i1:alter{parts = {1, 'number'}}
s:insert({-33.33})
i1:alter{parts = {1, 'scalar'}}
s:insert({44.44})
s:insert({"Hello world"})
s:select()
s:drop()
