local uuid = require('uuid')
local msgpack = require('msgpack')
local t = require('luatest')

local group_versions = t.group('group_versions', {
    -- UUID examples taken from the RFC 9562 edition:
    -- https://datatracker.ietf.org/doc/html/rfc9562#name-test-vectors

    -- The v2 implementation is not specified. Thus, it is
    -- omitted here.
    {version = 'v1', uuid = 'c232ab00-9414-11ec-b3c8-9f6bdeced846'},
    {version = 'v3', uuid = '5df41881-3aed-3515-88a7-2f4a814cf09e'},
    {version = 'v4', uuid = '919108f7-52d1-4320-9bac-f847db4148a8'},
    {version = 'v5', uuid = '2ed6657d-e927-568b-95e1-2665a8aea6a2'},
    {version = 'v6', uuid = '1ec9414c-232a-6b00-b3c8-9f6bdeced846'},
    {version = 'v7', uuid = '017f22e2-79b0-7cc3-98c4-dc0c0c07398f'},
    {version = 'v8_time_based', uuid = '2489e9ad-2ee2-8e00-8ec9-32d5f69181c0'},
    {version = 'v8_name_based', uuid = '5c146b14-3c52-8afd-938a-375d0df1fbf6'},
})

-- Base test.
group_versions.test_version = function(cg)
    local version = cg.params.version
    local u = cg.params.uuid
    t.assert(uuid.fromstr(u),
             ('UUID %s value (%s) failed to parse'):format(version, u))
end

local group_gh_5444 = t.group('group_gh_5444', {
    {uuid = 'bea80698-e07d-11ea-fe85-00155d373b0c'},
    {uuid = 'bee815b2-e07d-11ea-fe85-00155d373b0c'},
    {uuid = '4429d312-18d4-11eb-94f6-77e22d44915a'},
    {uuid = '64ecacb4-18d4-11eb-94f6-cbb989f20a7e'},
    {uuid = '64ecacb5-18d4-11eb-94f6-cf778123fd70'},
    {uuid = '00000000-0000-0000-e000-000000000000'},
    {uuid = '98c0cfc3-2b03-461d-b662-9a869bf46c75'},
})

-- Test all examples from gh-5444.
group_gh_5444.test_all_examples = function(cg)
    local u = cg.params.uuid
    local uuid_cdata = uuid.fromstr(u)
    t.assert(uuid_cdata, ('UUID value (%s) failed to parse'):format(u))
    t.assert(msgpack.decode(msgpack.encode(uuid_cdata)),
             ('UUID value (%s) failed to be decoded in msgpack'):format(u))
end
