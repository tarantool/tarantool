-- Check that collation is allowed only for strings, scalar and any types.
format = {}
format[1] = {'field1', 'unsigned', collation = 'unicode'}
s = box.schema.create_space('test', {format = format})
format[1] = {'field2', 'array', collation = 'unicode_ci'}
s = box.schema.create_space('test', {format = format})
