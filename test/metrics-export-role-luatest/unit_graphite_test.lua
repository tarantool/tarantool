local h = require('test.metrics-export-role-luatest.helper')
if h.skip_if_no_http_server() then
    return
end

require('third_party.metrics-export-role.test.unit.graphite_test')
