--
-- gh-2937: allow to specify collation in field definition.
--
format = {}
format[1] = {name = 'field1', type = 'string', collation = 'unicode'}
format[2] = {'field2', 'any', collation = 'unicode_ci'}
format[3] = {type = 'scalar', name = 'field3', collation = 'unicode'}
s = box.schema.create_space('test', {format = format})
s:format()
s:drop()
