box.once()
box.once("key")
box.once("key", "key")
box.once("key", nil)
box.once("key", function() end)

once  = nil
function f(arg) if once ~= nil then once = once + arg else once = arg end end
box.once("test", f, 1)
once
box.once("test", f, 1)
once
