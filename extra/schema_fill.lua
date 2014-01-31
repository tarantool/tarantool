_schema = box.space[box.schema.SCHEMA_ID]
_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
-- define schema version
_schema:insert{'version', 1, 6}
-- define system spaces
_space:insert{_schema.n, 0, '_schema'}
_space:insert{_space.n, 0, '_space'}
_space:insert{_index.n, 0, '_index'}
-- define indexes
_index:insert{_schema.n, 0, 'primary', 'tree', 1, 1, 0, 'str'}

-- space name is unique
_index:insert{_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_space.n, 1, 'name', 'tree', 1, 1, 2, 'str'}

-- index name is unique within a space
_index:insert{_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num'}
_index:insert{_index.n, 1, 'name', 'tree', 1, 2, 0, 'num', 2, 'str'}
-- 
