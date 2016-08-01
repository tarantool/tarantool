-- Tests for HASH index type

s3 = box.schema.space.create('my_space4')
i3_1 = s3:create_index('my_space4_idx1', {type='HASH', parts={1, 'scalar', 2, 'integer', 3, 'number'}, unique=true})
i3_2 = s3:create_index('my_space4_idx2', {type='HASH', parts={4, 'string', 5, 'scalar'}, unique=true})
s3:insert({100.5, 30, 95, "str1", 5})
s3:insert({"abc#$23", 1000, -21.542, "namesurname", 99})
s3:insert({true, -459, 4000, "foobar", "36.6"})
s3:select{}

i3_1:select({100.5})
i3_1:select({true, -459})
i3_1:select({"abc#$23", 1000, -21.542})

i3_2:select({"str1", 5})
i3_2:select({"str"})
i3_2:select({"str", 5})
i3_2:select({"foobar", "36.6"})

s3:drop()
