lua _schema = box.space[box.schema.SCHEMA_ID]
lua _space = box.space[box.schema.SPACE_ID]
lua _index = box.space[box.schema.INDEX_ID]
-- define schema version
lua _schema:insert('version', 1, 6)
-- define system spaces
lua _space:insert(_schema.n, 0, '_schema')
lua _space:insert(_space.n, 0, '_space')
lua _space:insert(_index.n, 0, '_index')
-- define indexes
lua _index:insert(_schema.n, 0, 'primary', 'tree', 1, 1, 0, 'str')

-- space name is unique
lua _index:insert(_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num')
lua _index:insert(_space.n, 1, 'name', 'tree', 1, 1, 2, 'str')

-- index name is unique within a space
lua _index:insert(_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num')
lua _index:insert(_index.n, 1, 'name', 'tree', 1, 2, 0, 'num', 2, 'str')
-- 
