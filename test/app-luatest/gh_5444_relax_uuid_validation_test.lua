local uuid = require('uuid')
local t = require("luatest")

local g = t.group()

g.test_all_versions = function()
    local values = {
        -- UUID values generated with https://www.uuidtools.com/.
        -- As a namespace ns.URL:tarantool.org is used.
        v1 = '7ff4307c-48f1-11ee-ac1a-325096b39f47',
        v2 = '000003e8-48f1-21ee-b000-325096b39f47',
        v3 = 'da8e829c-c63e-33df-af97-359b96647e2d',
        v4 = 'ff4b9fc2-620e-4e5c-a1b9-ff241c4793f0',
        v5 = 'a658ebe8-3c03-566d-bb9b-069247787734',
        -- UUID examples for new versions taken from the IETF
        -- draft: https://www.ietf.org/archive/id/draft-peabody-dispatch-new-uuid-format-04.html.
        v6 = '1ec9414c-232a-6b00-b3c8-9e6bdeced846',
        v7 = '017f22e2-79b0-7cc3-98c4-dc0c0c07398f',
        v8 = '320c3d4d-cc00-875b-8ec9-32d5f69181c0',
    }

    for v, u in pairs(values) do
        t.assert(uuid.fromstr(u),
                 ('UUID %s value (%s) failed to parse'):format(v, u))
    end
end

g.test_all_examples_from_gh_5444 = function()
    local values = {
        'bea80698-e07d-11ea-fe85-00155d373b0c',
        'bee815b2-e07d-11ea-fe85-00155d373b0c',
        '4429d312-18d4-11eb-94f6-77e22d44915a',
        '64ecacb4-18d4-11eb-94f6-cbb989f20a7e',
        '64ecacb5-18d4-11eb-94f6-cf778123fd70',
        '00000000-0000-0000-e000-000000000000',
        '98c0cfc3-2b03-461d-b662-9a869bf46c75',
    }

    for _, u in pairs(values) do
        t.assert(uuid.fromstr(u),
                 ('UUID value (%s) failed to parse'):format(u))
    end
end
