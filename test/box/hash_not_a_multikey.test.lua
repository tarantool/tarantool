-- Hash index cannot be multikey.

s = box.schema.space.create('test')
_ = s:create_index('primary')
_ = s:create_index('hash', {type = 'hash', parts = {{'[2][*]', 'unsigned'}}})
s:drop()
