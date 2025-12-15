return setmetatable({}, {
  __index = function(self, k)
    assert(type(k) == 'string', "The name of `utils' submodule is required")
    rawset(self, k, require(('utils.%s'):format(k)))
    return rawget(self, k)
  end,
})
