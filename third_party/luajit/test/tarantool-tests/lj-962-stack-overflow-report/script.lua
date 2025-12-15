-- XXX: Function `f` is global to avoid using an additional stack
-- slot.
-- luacheck: no global
f = function() f() end; f()
