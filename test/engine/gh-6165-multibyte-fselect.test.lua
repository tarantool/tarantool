-- https://github.com/tarantool/tarantool/issues/6165
-- formatted select with multibyte characters
inspector = require('test_run').new()
engine = inspector:get_cfg('engine')

s = box.schema.space.create('test', {engine = engine, format = {{'name', 'string'}, {'surname', 'string'}}})
_ = s:create_index('pk')
_ = s:insert({'Ян', 'Ким'})
s:fselect()
s:drop()

s = box.schema.space.create('test', {engine = engine, format = {{'name', 'string'}, {'surname', 'string'}}})
_ = s:create_index('pk')
_ = s:insert({'Абвгде', 'Жзийклмн'})
s:fselect()
s:drop()

s = box.schema.space.create('test', {engine = engine, format = {{'Первый столбец', 'string'}, {'Второй столбец', 'string'}}})
_ = s:create_index('pk')
_ = s:insert({'abcdef', 'cde'})
s:fselect()
s:drop()

s = box.schema.space.create('test', {engine = engine, format = {{'Первый столбец', 'string'}, {'Второй столбец', 'string'}}})
_ = s:create_index('pk')
_ = s:insert({'абв', 'гдежз'})
s:fselect()
s:drop()

-- The problem: alignment will be still incorrect if characters have variable length
s = box.schema.space.create('test', {engine = engine, format = {{'Abcde', 'string'}, {'Qwert', 'string'}}})
_ = s:create_index('pk')
_ = s:insert({'абвгд', '非常长的词'})
s:fselect()
s:drop()
