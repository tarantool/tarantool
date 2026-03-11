local t = require('luatest')

local g = t.group()

g.test_gh_7255_box_info_before_box_cfg = function()
    local info = box.info()
    t.assert_equals(info.status, "unconfigured")
end

g.test_gh_9173_all_box_info = function()
    -- Check that application won't be crashed
    for _, func in pairs(box.info()) do
        pcall(func)
    end
end

g.test_gh_9173_box_info_errors = function()
    t.assert_error_msg_equals('Please call box.cfg{} first',
                              box.info.memory)
    t.assert_error_msg_equals('Please call box.cfg{} first',
                              box.info.sql)
    t.assert_error_msg_equals('Please call box.cfg{} first',
                              box.info.vinyl)
    t.assert_error_msg_equals('Please call box.cfg{} first',
                              box.info.gc)
    t.assert_equals(box.info.replication_anon(), {},
                    'Works ok without box.cfg{}')
end
