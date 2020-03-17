-- Hash index can not use function.

s = box.schema.space.create('withdata')
lua_code = [[function(tuple) return tuple[1] + tuple[2] end]]
box.schema.func.create('s', {body = lua_code, is_deterministic = true, is_sandboxed = true})
_ = s:create_index('pk')
_ = s:create_index('idx', {type = 'hash', func = box.func.s.id, parts = {{1, 'unsigned'}}})
s:drop()
box.schema.func.drop('s')
