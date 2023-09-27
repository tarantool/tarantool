local t = require('luatest')

local g = t.group()

g.test_all_box_info = function()
    -- Check that application won't be crashed
    for _, func in pairs(box.info()) do
        pcall(func)
    end
end

g.test_box_info_errors = function()
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
