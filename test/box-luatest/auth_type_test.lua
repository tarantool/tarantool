local fio = require('fio')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')
local urilib = require('uri')

local AUTH_TYPES = {'chap-sha1', 'pap-sha256'}
local CERT_DIR = fio.pathjoin(fio.abspath(os.getenv('SOURCEDIR') or '.'),
                              'test/ssl_cert')
local CERT_FILE = fio.pathjoin(CERT_DIR, 'server.crt')
local KEY_FILE = fio.pathjoin(CERT_DIR, 'server.key')

-- Returns an array of {user, password, auth_type, expected_success}.
local function make_auth_test_data(test_auth_type)
    local ret = {
        {'guest', '', test_auth_type, test_auth_type == 'chap-sha1'},
        {'guest', 'password', test_auth_type, false},
        {'no-such-user', 'secret', test_auth_type, false},
    }
    for _, auth_type in ipairs(AUTH_TYPES) do
        table.insert(ret, {'test-' .. auth_type, 'secret', test_auth_type,
                           auth_type == test_auth_type})
        -- Test the default authentication type reported by the server.
        table.insert(ret, {'test-' .. auth_type, 'secret', box.NULL,
                           auth_type == test_auth_type})
    end
    return ret
end

local common = t.group('auth_type.common', t.helpers.matrix({
    auth_type = AUTH_TYPES,
    transport = {'plain', 'ssl'},
}))

common.before_all(function(cg)
    t.skip_if(cg.params.auth_type == 'pap-sha256' and
              cg.params.transport == 'plain',
              "Authentication method 'pap-sha256' " ..
              "does not support unencrypted connection")
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(transport, cert_file, key_file,
                            auth_types, test_auth_type)
        box.cfg({
            listen = {
                uri = box.cfg.listen,
                params = {
                    transport = transport,
                    ssl_cert_file = cert_file,
                    ssl_key_file = key_file,
                }
            }
        })
        for _, auth_type in ipairs(auth_types) do
            local user = 'test-' .. auth_type
            box.cfg({auth_type = auth_type})
            box.schema.user.create(user, {password = 'secret'})
            box.session.su('admin', box.schema.user.grant, user, 'super')
        end
        box.cfg({auth_type = test_auth_type})
    end, {cg.params.transport, CERT_FILE, KEY_FILE, AUTH_TYPES,
          cg.params.auth_type})
end)

common.after_all(function(cg)
    cg.server:drop()
end)

common.test_net_box = function(cg)
    local parsed_uri = urilib.parse(cg.server.net_box_uri)
    parsed_uri.params = parsed_uri.params or {}
    parsed_uri.params.transport = {cg.params.transport}
    for _, data in pairs(make_auth_test_data(cg.params.auth_type)) do
        local user, password, auth_type, expected_success = unpack(data)
        if auth_type == box.NULL then
            auth_type = nil
        end
        local msg = string.format("user='%s', password='%s', auth_type=%s",
                                  user, password, auth_type)
        local expected_state, expected_errmsg = 'active', nil
        if not expected_success then
            expected_state, expected_errmsg =
                'error',
                'User not found or supplied credentials are invalid'
        end
        parsed_uri.login = user
        parsed_uri.password = password
        parsed_uri.params.auth_type = auth_type and {auth_type} or nil
        local conn = net.connect(urilib.format(parsed_uri, true))
        t.assert_equals(conn.state, expected_state, msg)
        t.assert_equals(conn.error, expected_errmsg, msg)
        conn:close()
    end
end

common.before_test('test_replication', function(cg)
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = {
                uri = cg.server.net_box_uri,
                params = {
                    transport = cg.params.transport,
                    ssl_cert_file = CERT_FILE,
                    ssl_key_file = KEY_FILE,
                }
            },
            replication_connect_quorum = 0,
        },
    })
    cg.replica:start()
    cg.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
end)

common.after_test('test_replication', function(cg)
    cg.replica:drop()
end)

common.test_replication = function(cg)
    cg.replica:exec(function(uri, transport, auth_test_data)
        local urilib = require('uri')
        local parsed_uri = urilib.parse(uri)
        parsed_uri.params = parsed_uri.params or {}
        parsed_uri.params.transport = {transport}
        for _, data in pairs(auth_test_data) do
            local user, password, auth_type, expected_success = unpack(data)
            if auth_type == box.NULL then
                auth_type = nil
            end
            local msg = string.format("user='%s', password='%s', auth_type=%s",
                                      user, password, auth_type)
            local expected_status, expected_errmsg = 'follow', nil
            if not expected_success then
                expected_status, expected_errmsg =
                    'loading',
                    'User not found or supplied credentials are invalid'
            end
            parsed_uri.login = user
            parsed_uri.password = password
            parsed_uri.params.auth_type = auth_type and {auth_type} or nil
            box.cfg({replication = urilib.format(parsed_uri, true)})
            t.helpers.retrying({}, function()
                t.assert(box.info.replication[1].upstream)
                t.assert_equals(box.info.replication[1].upstream.status,
                                expected_status, msg)
                t.assert_equals(box.info.replication[1].upstream.message,
                                expected_errmsg, msg)
            end)
        end
    end, {cg.server.net_box_uri, cg.params.transport,
          make_auth_test_data(cg.params.auth_type)})
end

local chap_sha1 = t.group('auth_type.chap-sha1')

chap_sha1.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

chap_sha1.after_all(function(cg)
    cg.server:drop()
end)

chap_sha1.test_default = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.cfg.auth_type, 'chap-sha1')
    end)
end

chap_sha1.after_test('test_scramble', function(cg)
    cg.server:exec(function()
        box.schema.user.drop('sarah', {if_exists = true})
    end)
end)

chap_sha1.test_scramble = function(cg)
    cg.server:exec(function()
        local digest = require('digest')
        box.schema.user.create('sarah', {password = 'SARAH'})
        local auth = box.space._user.index.name:get('sarah').auth
        t.assert_type(auth, 'table')
        local data = auth['chap-sha1']
        t.assert(data)
        t.assert_equals(auth, {['chap-sha1'] = data})
        t.assert_type(data, 'string')
        t.assert_equals(#data, 28)
        local scramble = digest.base64_decode(data)
        t.assert_equals(digest.sha1(digest.sha1('SARAH')), scramble)
    end)
end

local pap_sha256 = t.group('auth_type.pap-sha256')

pap_sha256.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {auth_type = 'pap-sha256'},
    })
    cg.server:start()
    cg.server:exec(function()
        box.schema.user.create('test', {password = 'secret'})
        box.session.su('admin', box.schema.user.grant, 'test', 'super')
    end)
end)

pap_sha256.after_all(function(cg)
    cg.server:drop()
end)

pap_sha256.after_test('test_password_hash', function(cg)
    cg.server:exec(function()
        box.schema.user.drop('alice', {if_exists = true})
    end)
end)

pap_sha256.test_password_hash = function(cg)
    cg.server:exec(function()
        local digest = require('digest')
        box.schema.user.create('alice', {password = 'ALICE'})
        local auth = box.space._user.index.name:get('alice').auth
        t.assert_type(auth, 'table')
        local data = auth['pap-sha256']
        t.assert(data)
        t.assert_equals(auth, {['pap-sha256'] = data})
        t.assert_equals(#data, 2)
        t.assert_equals(#data[1], 28)
        t.assert_equals(#data[2], 44)
        local salt = digest.base64_decode(data[1])
        local hash = digest.base64_decode(data[2])
        t.assert_equals(digest.sha256(salt .. 'ALICE'), hash)
    end)
end

pap_sha256.after_test('test_password_salt', function(cg)
    cg.server:exec(function()
        box.schema.user.drop('bob', {if_exists = true})
    end)
end)

pap_sha256.test_password_salt = function(cg)
    cg.server:exec(function()
        box.schema.user.create('bob', {password = 'BOB'})
        local auth1 = box.space._user.index.name:get('bob').auth
        t.assert_type(auth1, 'table')
        t.assert(auth1['pap-sha256'])
        box.schema.user.passwd('bob', 'BOB')
        local auth2 = box.space._user.index.name:get('bob').auth
        t.assert_type(auth2, 'table')
        t.assert(auth2['pap-sha256'])
        t.assert_not_equals(auth1, auth2)
    end)
end

pap_sha256.test_unencrypted_net_box = function(cg)
    local parsed_uri = urilib.parse(cg.server.net_box_uri)
    parsed_uri.login = 'test'
    parsed_uri.password = 'secret'
    parsed_uri.params = parsed_uri.params or {}
    parsed_uri.params.auth_type = {'pap-sha256'}
    local conn = net.connect(urilib.format(parsed_uri, true))
    t.assert_equals(conn.state, 'error')
    t.assert_equals(conn.error,
                    "Authentication method 'pap-sha256' " ..
                    "does not support unencrypted connection")
    conn:close()
end

pap_sha256.before_test('test_unencrypted_replication', function(cg)
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = cg.server.net_box_uri,
            replication_connect_quorum = 0,
        },
    })
    cg.replica:start()
    cg.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
end)

pap_sha256.after_test('test_unencrypted_replication', function(cg)
    cg.replica:drop()
end)

pap_sha256.test_unencrypted_replication = function(cg)
    cg.replica:exec(function(uri)
        local urilib = require('uri')
        local parsed_uri = urilib.parse(uri)
        parsed_uri.login = 'test'
        parsed_uri.password = 'secret'
        parsed_uri.params = parsed_uri.params or {}
        parsed_uri.params.auth_type = {'pap-sha256'}
        box.cfg({replication = urilib.format(parsed_uri, true)})
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status,
                            'stopped')
            t.assert_equals(box.info.replication[1].upstream.message,
                            "Authentication method 'pap-sha256' " ..
                            "does not support unencrypted connection")
        end)
    end, {cg.server.net_box_uri})
end
