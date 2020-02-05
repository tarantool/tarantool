-- Check that error is raised when collation with wrong id is used.
_space = box.space[box.schema.SPACE_ID]
utils = require('utils')
EMPTY_MAP = utils.setmap({})
format = {{name = 'field1', type = 'string', collation = 666}}
surrogate_space = {12345, 1, 'test', 'memtx', 0, EMPTY_MAP, format}
_space:insert(surrogate_space)
