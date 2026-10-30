-- Check that error is raised when collation doesn't exists.
format = {}
format[1] = {'field1', 'unsigend', collation = 'test_coll'}
s = box.schema.create_space('test', {format = format})
