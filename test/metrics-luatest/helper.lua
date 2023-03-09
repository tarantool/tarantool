-- Assert running tests with built-in metrics.
require('metrics')
require('third_party.metrics.test.rock_utils').assert_builtin('metrics')

-- Workaround paths to reuse existing submodule tests.
local workaround_requires = function(path)
    package.preload[path] = function()
        return require('third_party.metrics.' .. path)
    end
end

workaround_requires('test.utils')
workaround_requires('test.psutils_linux_test_payload')
