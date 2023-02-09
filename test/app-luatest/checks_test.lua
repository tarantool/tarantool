local checks = require('checks')

local package_source = debug.getinfo(checks).source
assert(package_source:match('^@builtin') ~= nil,
       "Run tests for built-in checks package")

require('third_party.checks.test.test')
