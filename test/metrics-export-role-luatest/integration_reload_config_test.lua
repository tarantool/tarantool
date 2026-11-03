require('test.metrics-export-role-luatest.helper')

if jit.os == 'OSX' then
    local t = require('luatest')
    local g = t.group()
    g.test_skip_on_macos = function()
        t.skip('the upstream test requires Linux loopback semantics')
    end
    return
end

require('third_party.metrics-export-role.test.integration.reload_config_test')
