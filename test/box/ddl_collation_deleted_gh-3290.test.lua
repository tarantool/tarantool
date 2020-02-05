--
-- gh-3290: expose ICU into Lua. It uses built-in collations, that
-- must work even if a collation is deleted from _collation.
--
t = box.space._collation:delete{1}
utf8.cmp('abc', 'def')
box.space._collation:replace(t)
