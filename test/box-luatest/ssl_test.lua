local fio = require('fio')
local fun = require('fun')
local ffi = require('ffi')
local it = require('test.interactive_tarantool')
local net = require('net.box')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')
local tarantool = require('tarantool')
local t = require('luatest')
local g = t.group()

ffi.cdef([[
    long ssl_openssl_version_number(void);
]])

local VARDIR = server.vardir
local CERT_DIR = fio.pathjoin(fio.abspath(os.getenv('SOURCEDIR') or '.'),
                              'test/ssl_cert')
local CA_FILE = fio.pathjoin(CERT_DIR, 'ca.crt')
local SERVER_CERT_FILE = fio.pathjoin(CERT_DIR, 'server.crt')
local SERVER_KEY_FILE = fio.pathjoin(CERT_DIR, 'server.key')
local SERVER_KEY_FILE_ENC = fio.pathjoin(CERT_DIR, 'server.enc.key')
local CLIENT_CERT_FILE = fio.pathjoin(CERT_DIR, 'client.crt')
local CLIENT_KEY_FILE = fio.pathjoin(CERT_DIR, 'client.key')
local CLIENT_KEY_FILE_ENC = fio.pathjoin(CERT_DIR, 'client.enc.key')
local CA2_FILE = fio.pathjoin(CERT_DIR, 'ca2.crt')
local SERVER2_CERT_FILE = fio.pathjoin(CERT_DIR, 'server2.crt')
local SERVER2_KEY_FILE = fio.pathjoin(CERT_DIR, 'server2.key')
local CLIENT2_CERT_FILE = fio.pathjoin(CERT_DIR, 'client2.crt')
local CLIENT2_KEY_FILE = fio.pathjoin(CERT_DIR, 'client2.key')
local TMP_CERT_FILE = fio.pathjoin(VARDIR, 'tmp.crt')
local TMP_KEY_FILE = fio.pathjoin(VARDIR, 'tmp.key')
local GOST_KEY_FILE = fio.pathjoin(CERT_DIR, 'gost.key')
local GOST_CERT_FILE = fio.pathjoin(CERT_DIR, 'gost.crt')
local SENSITIVE_SSL_DATA = {
    ssl_password = "123qwe", ssl_cert_file = CLIENT_CERT_FILE,
    ssl_key_file = CLIENT_KEY_FILE_ENC, ssl_ca_file = CA_FILE,
}
local SENSITIVE_URI = "localhost:0?transport=ssl"
local SAFE_URI = "localhost:0"
for ssl_key, ssl_value in pairs(SENSITIVE_SSL_DATA) do
    SENSITIVE_URI = SENSITIVE_URI .. '&' ..
                    string.format('%s=%s', ssl_key, ssl_value)
end
local OPENSSL_VERSION_NUMBER = ffi.C.ssl_openssl_version_number()
local OPENSSL_3_0_0_OR_NEWER = (OPENSSL_VERSION_NUMBER >=
                                tonumber('30000000', 16))

g.before_test('test_invalid_cfg', function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_test('test_invalid_cfg', function()
    g.server:stop()
    g.server = nil
end)

g.test_invalid_cfg = function()
    local TEST_CASES = {
        {
            uri_params = {
                transport = 'ssl',
                ssl_ca_file = '/no/such/file',
                ssl_cert_file = SERVER_CERT_FILE,
                ssl_key_file = SERVER_KEY_FILE,
            },
            error_msg = "Error loading SSL CA '/no/such/file': " ..
                        "No such file or directory",
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = '/no/such/file',
                ssl_key_file = SERVER_KEY_FILE,
            },
            error_msg = "Error loading SSL certificate '/no/such/file': " ..
                        "No such file or directory",
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = SERVER_CERT_FILE,
                ssl_key_file = '/no/such/file',
            },
            error_msg = "Error loading SSL private key '/no/such/file': " ..
                        "No such file or directory",
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = SERVER_CERT_FILE,
                ssl_key_file = SERVER_KEY_FILE,
                ssl_password_file = '/no/such/file',
            },
            error_msg = "Error reading SSL password file '/no/such/file': " ..
                        "No such file or directory",
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = SERVER_KEY_FILE,
                ssl_key_file = SERVER_KEY_FILE,
            },
            error_msg = string.format("Error loading SSL certificate '%s': " ..
                                      "no start line", SERVER_KEY_FILE)
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = SERVER_CERT_FILE,
                ssl_key_file = SERVER_CERT_FILE,
            },
            -- The error message depends on the OpenSSL version.
            error_msg = fun.map(function(v)
                return string.format("Error loading SSL private key '%s': %s",
                                     SERVER_CERT_FILE, v)
            end, {'unsupported', 'PEM lib', 'no start line'}):totable()
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = CA_FILE,
                ssl_key_file = SERVER_KEY_FILE,
            },
            error_msg = string.format("Error loading SSL private key '%s': " ..
                                      "key values mismatch", SERVER_KEY_FILE),
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = SERVER_CERT_FILE,
                ssl_key_file = SERVER_KEY_FILE_ENC,
            },
            error_msg = string.format("Error loading SSL private key '%s': " ..
                                      "bad decrypt", SERVER_KEY_FILE_ENC),
        },
    }
    local LISTEN_TEST_CASES = {
        {
            uri_params = {
                transport = 'ssl',
                ssl_key_file = SERVER_KEY_FILE,
            },
            error_msg = "SSL certificate missing",
        },
        {
            uri_params = {
                transport = 'ssl',
                ssl_cert_file = SERVER_CERT_FILE,
            },
            error_msg = "SSL private key missing",
        },
    }
    g.server:exec(function(uri, test_cases, listen_test_cases)
        local net = require('net.box')
        local function check_error_msg(error_msg, f, ...)
            if type(error_msg) ~= 'table' then
                error_msg = {error_msg}
            end
            local ok, err = pcall(f, ...)
            t.assert_items_include(error_msg, {ok or err.message})
        end
        -- box.cfg.listen and box.cfg.replication
        for _, v in ipairs(test_cases) do
            check_error_msg(
                v.error_msg,
                box.cfg, {listen = {uri = uri, params = v.uri_params}})
            check_error_msg(
                v.error_msg,
                box.cfg, {replication = {uri = uri, params = v.uri_params}})
        end
        for _, v in ipairs(listen_test_cases) do
            check_error_msg(
                v.error_msg,
                box.cfg, {listen = {uri = uri, params = v.uri_params}})
        end
        -- net.box.connect
        for _, v in ipairs(test_cases) do
            check_error_msg(
                v.error_msg,
                net.connect, {uri = uri, params = v.uri_params})
        end
    end, {g.server.net_box_uri, TEST_CASES, LISTEN_TEST_CASES})
end

g.before_test('test_net_box', function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function(uri, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.server.net_box_uri, SERVER_CERT_FILE, SERVER_KEY_FILE})
end)

g.after_test('test_net_box', function()
    g.server:stop()
    g.server = nil
end)

g.test_net_box = function()
    local c
    local uri = g.server.net_box_uri

    -- Attempt to establish unencrypted connection should fail.
    c = net.connect(uri, {connect_timeout = 0.1})
    t.assert_equals(c.state, 'error')
    c:close()

    -- No CA and client certificate is okay.
    c = net.connect({uri = uri, params = {transport = 'ssl'}})
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Passing client certificate is okay.
    c = net.connect({
        uri = uri,
        params = {
            transport = 'ssl',
            ssl_cert_file = CLIENT_CERT_FILE,
            ssl_key_file = CLIENT_KEY_FILE,
        }
    })
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Check server authority.
    c = net.connect({
        uri = uri,
        params = {
            transport = 'ssl',
            ssl_ca_file = CLIENT_CERT_FILE,
        }
    })
    t.assert_equals(c.state, 'error')
    t.assert_str_contains(c.error, 'certificate verify failed')
    c:close()
    c = net.connect({
        uri = uri,
        params = {
            transport = 'ssl',
            ssl_ca_file = CA_FILE,
        }
    })
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Enforce client authentication.
    g.server:exec(function(listen_uri, ca_file, cert_file, key_file)
        box.cfg({listen = {
            uri = listen_uri,
            params = {
                transport = 'ssl',
                ssl_ca_file = ca_file,
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {uri, CA_FILE, SERVER_CERT_FILE, SERVER_KEY_FILE})

    -- Check client authority.
    c = net.connect({uri = uri, params = {transport = 'ssl'}})
    t.assert_equals(c.state, 'error')
    t.assert_str_contains(c.error, 'handshake failure')
    c:close()
    c = net.connect({
        uri = uri,
        params = {
            transport = 'ssl',
            ssl_cert_file = CLIENT_CERT_FILE,
            ssl_key_file = CLIENT_KEY_FILE,
        }
    })
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()
end

g.before_test('test_client_disconnect', function()
    t.skip_if(not OPENSSL_3_0_0_OR_NEWER)
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function(uri, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.server.net_box_uri, SERVER_CERT_FILE, SERVER_KEY_FILE})
end)

g.after_test('test_client_disconnect', function()
    g.server:stop()
    g.server = nil
end)

-- gh-1534. Check server does not log client disconnect as SSL error.
g.test_client_disconnect = function()
    local uri = g.server.net_box_uri
    local c = net.connect({uri = uri, params = {transport = 'ssl'}})
    c:close()
    -- On stop we finish close all connections. So the above will be finished.
    g.server:stop()
    t.assert_equals(g.server:grep_log('E>.*unexpected eof while reading'), nil)
end

g.before_test('test_replication', function()
    g.cluster = cluster:new({})
    g.master = g.cluster:build_and_add_server({alias = 'master'})
    g.master:start()
    g.master:exec(function(uri, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.master.net_box_uri, SERVER_CERT_FILE, SERVER_KEY_FILE})
    g.replica = g.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {
                uri = g.master.net_box_uri,
                params = {transport = 'ssl'}
            },
            replication_connect_quorum = 1,
            replication_sync_timeout = 300,
        }
    })
    g.replica:start()
end)

g.after_test('test_replication', function()
    g.cluster:stop()
    g.cluster = nil
    g.master = nil
    g.replica = nil
end)

g.test_replication = function()
    g.replica:exec(function(uri, ca_file, cert_file, key_file)
        local replication_connect_timeout = box.cfg.replication_connect_timeout

        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')

        -- Attempt to establish unencrypted connection should fail.
        box.cfg({replication_connect_timeout = 0.1})
        box.cfg({replication = uri})
        box.cfg({replication_connect_timeout = replication_connect_timeout})
        t.assert_equals(box.info.status, 'orphan')
        t.assert_not(box.info.replication[1].upstream)
        box.cfg({replication = {}})

        -- No CA and client certificate is okay.
        box.cfg({replication = {
            uri = uri,
            params = {transport = 'ssl'}
        }})
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {}})

        -- Passing client certificate is okay.
        box.cfg({replication = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {}})

        -- Check server authority,
        box.cfg({replication_connect_timeout = 0.1})
        box.cfg({replication = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_ca_file = cert_file,
            }
        }})
        box.cfg({replication_connect_timeout = replication_connect_timeout})
        t.assert_equals(box.info.status, 'orphan')
        t.assert_not(box.info.replication[1].upstream)
        box.cfg({replication = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_ca_file = ca_file,
            }
        }})
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {}})
    end, {g.master.net_box_uri, CA_FILE, CLIENT_CERT_FILE, CLIENT_KEY_FILE})

    g.master:exec(function(uri, ca_file, cert_file, key_file)
        -- Enforce client authentication.
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_ca_file = ca_file,
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.master.net_box_uri, CA_FILE, SERVER_CERT_FILE, SERVER_KEY_FILE})

    g.replica:exec(function(uri, cert_file, key_file)
        local replication_connect_timeout = box.cfg.replication_connect_timeout

        -- Check client authority.
        box.cfg({replication_connect_timeout = 0.1})
        box.cfg({replication = {
            uri = uri,
            params = {transport = 'ssl'}
        }})
        box.cfg({replication_connect_timeout = replication_connect_timeout})
        t.assert_equals(box.info.status, 'orphan')
        t.assert_not(box.info.replication[1].upstream)
        box.cfg({replication = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {}})
    end, {g.master.net_box_uri, CLIENT_CERT_FILE, CLIENT_KEY_FILE})
end

g.before_test('test_replication_reconnect_after_enabling_ssl', function()
    g.cluster = cluster:new({})
    g.master = g.cluster:build_and_add_server({alias = 'master'})
    g.master:start()
    g.replica = g.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = g.master.net_box_uri,
            replication_timeout = 0.1,
            replication_connect_timeout = 0.1,
            replication_connect_quorum = 1,
            replication_sync_timeout = 300,
        }
    })
    g.replica:start()
end)

g.after_test('test_replication_reconnect_after_enabling_ssl', function()
    g.cluster:stop()
    g.cluster = nil
    g.master = nil
    g.replica = nil
end)

--
-- Enables encryption first on the replica, then on the master. Checks that
-- the replica reconnects successfully (gh-107).
--
g.test_replication_reconnect_after_enabling_ssl = function()
    -- Enable encryption on the replica.
    -- The connection should fail, because the master doesn't use encryption.
    g.replica:exec(function(uri)
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {
            uri = uri,
            params = {transport = 'ssl'}
        }})
        t.assert_equals(box.info.status, 'orphan')
        t.assert_not(box.info.replication[1].upstream)
    end, {g.master.net_box_uri})

    -- Enable encryption on the master.
    g.master:exec(function(uri, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.master.net_box_uri, SERVER_CERT_FILE, SERVER_KEY_FILE})

    -- Wait for the replica to connect.
    g.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.status, 'running')
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
end

g.before_test('test_replication_reconnect_after_disabling_ssl', function()
    g.cluster = cluster:new({})
    g.master = g.cluster:build_and_add_server({alias = 'master'})
    g.master:start()
    g.master:exec(function(uri, cert_file, key_file)
        box.cfg({
            listen = {
                uri = uri,
                params = {
                    transport = 'ssl',
                    ssl_cert_file = cert_file,
                    ssl_key_file = key_file,
                },
            },
            replication_timeout = 0.1,
        })
    end, {g.master.net_box_uri, SERVER_CERT_FILE, SERVER_KEY_FILE})
    g.replica = g.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {
                uri = g.master.net_box_uri,
                params = {transport = 'ssl'},
            },
            replication_timeout = 0.1,
            replication_connect_timeout = 0.5,
            replication_connect_quorum = 1,
            replication_sync_timeout = 300,
        },
    })
    g.replica:start()
end)

g.after_test('test_replication_reconnect_after_disabling_ssl', function()
    g.cluster:stop()
    g.cluster = nil
    g.master = nil
    g.replica = nil
end)

--
-- Disables encryption first on the replica, then on the master. Checks that
-- the replica reconnects successfully (gh-137).
--
g.test_replication_reconnect_after_disabling_ssl = function()
    -- Disable encryption on the replica.
    -- The connection should fail, because the master still uses encryption.
    g.replica:exec(function(uri)
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = {
            uri = uri,
            params = {transport = 'plain'}
        }})
        t.assert_equals(box.info.status, 'orphan')
        t.assert_not(box.info.replication[1].upstream)
    end, {g.master.net_box_uri})

    -- Disable encryption on the master.
    g.master:exec(function(uri)
        box.cfg({listen = {
            uri = uri,
            params = {transport = 'plain'}
        }})
    end, {g.master.net_box_uri})

    -- Wait for the replica to connect.
    g.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.status, 'running')
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
end

g.before_test('test_ciphers', function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_test('test_ciphers', function()
    g.server:stop()
    g.server = nil
end)

g.test_ciphers = function()
    local uri = g.server.net_box_uri
    g.server:exec(function(uri, cert_file, key_file)
        local ssl_uri = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
                ssl_ciphers = 'FOO:BAR',
            }
        }
        t.assert_error_msg_equals(
            "Error setting SSL ciphers 'FOO:BAR': no cipher match",
            box.cfg, {listen = ssl_uri})
        ssl_uri.params.ssl_ciphers = 'ECDHE-RSA-AES256-GCM-SHA384'
        box.cfg({listen = ssl_uri})
    end, {uri, SERVER_CERT_FILE, SERVER_KEY_FILE})
    local ssl_uri = {
        uri = uri,
        params = {
            transport = 'ssl',
            ssl_ciphers = 'DHE-RSA-AES256-GCM-SHA384'
        }
    }
    local c = net.connect(ssl_uri)
    t.assert_equals(c.state, 'error')
    t.assert_str_contains(c.error, 'handshake failure')
    c:close()
    ssl_uri.params.ssl_ciphers = (ssl_uri.params.ssl_ciphers ..
                                  ':ECDHE-RSA-AES256-GCM-SHA384')
    c = net.connect(ssl_uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()
end

g.before_test('test_passwd', function()
    g.passwd_path = fio.pathjoin(VARDIR, 'passwd.txt')
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function(uri, ca_file, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_ca_file = ca_file,
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.server.net_box_uri, CA_FILE, SERVER_CERT_FILE, SERVER_KEY_FILE})
end)

g.after_test('test_passwd', function()
    g.server:stop()
    g.server = nil
    fio.unlink(g.passwd_path)
    g.passwd_path = nil
end)

g.test_passwd = function()
    local passwd_file = fio.open(g.passwd_path, {'O_WRONLY', 'O_CREAT'},
                                 tonumber('666', 8))
    t.assert_is_not(passwd_file, nil)

    local uri = {
        uri = g.server.net_box_uri,
        params = {
            transport = 'ssl',
            ssl_cert_file = CLIENT_CERT_FILE,
            ssl_key_file = CLIENT_KEY_FILE_ENC,
            ssl_password_file = g.passwd_path,
        }
    }
    local err = string.format("Error loading SSL private key '%s': bad decrypt",
                              CLIENT_KEY_FILE_ENC)
    local c

    -- Valid password in the password file.
    passwd_file:seek(0)
    passwd_file:truncate(0)
    passwd_file:write('123qwe\n')
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Valid password in the password file. No new line at EOF.
    passwd_file:seek(0)
    passwd_file:truncate(0)
    passwd_file:write('123qwe')
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Valid password in the middle of the password file.
    passwd_file:seek(0)
    passwd_file:truncate(0)
    passwd_file:write('foo\n123qwe\nbar\n')
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Empty line in the middle of the password file.
    passwd_file:seek(0)
    passwd_file:truncate(0)
    passwd_file:write('foo\n\n123qwe\n')
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Valid password in the password file,
    -- Invalid password in the password parameter.
    uri.params.ssl_password = 'fuzz'
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Empty password file.
    passwd_file:seek(0)
    passwd_file:truncate(0)
    t.assert_error_msg_equals(err, net.connect, uri)

    -- No matching password.
    passwd_file:seek(0)
    passwd_file:truncate(0)
    passwd_file:write('foo\nbar\nbaz\n')
    t.assert_error_msg_equals(err, net.connect, uri)

    -- Valid password in the password parameter.
    -- No matching password in the password file.
    uri.params.ssl_password = '123qwe'
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Valid password in the password parameter.
    -- No password file specified.
    uri.params.ssl_password_file = nil
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    -- Using a password with an unencrypted key is fine.
    uri = {
        uri = g.server.net_box_uri,
        params = {
            transport = 'ssl',
            ssl_cert_file = CLIENT_CERT_FILE,
            ssl_key_file = CLIENT_KEY_FILE,
            ssl_password = 'fuzz',
            ssl_password_file = g.passwd_path,
        }
    }
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()

    passwd_file:close()
end

g.before_test('test_gost', function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_test('test_gost', function()
    g.server:stop()
    g.server = nil
end)

g.test_gost = function()
    t.skip_if(tarantool.build.linking ~= 'static' or
              not t.tarantool.is_enterprise_package(),
              'GOST SSL engine is available only in static build of ' ..
              'Tarantool Enterprise Edition')
    local uri = g.server.net_box_uri
    g.server:exec(function(uri, cert_file, key_file)
        box.cfg({
            listen = {
                uri = uri,
                params = {
                    transport = 'ssl',
                    ssl_cert_file = cert_file,
                    ssl_key_file = key_file,
                    ssl_ciphers = 'GOST2012-GOST8912-GOST8912',
                }
            }
        })
    end, {uri, GOST_CERT_FILE, GOST_KEY_FILE})
    local c = net.connect({
        uri = uri,
        params = {transport = 'ssl'}
    })
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:eval('return true'), true)
    c:close()
end

g.before_test('test_reload_listen', function()
    t.assert(fio.copyfile(SERVER_CERT_FILE, TMP_CERT_FILE))
    t.assert(fio.copyfile(SERVER_KEY_FILE, TMP_KEY_FILE))
    g.server = server:new()
    g.server:start()
    g.server:exec(function(uri, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {g.server.net_box_uri, TMP_CERT_FILE, TMP_KEY_FILE})
end)

g.after_test('test_reload_listen', function()
    g.server:stop()
    g.server = nil
    g.reload = nil
    fio.unlink(TMP_CERT_FILE)
    fio.unlink(TMP_KEY_FILE)
end)

--
-- Checks that reconfiguring box.cfg.listen with the same URI triggers
-- reloading of SSL certificate files.
--
g.test_reload_listen = function()
    local uri = {
        uri = g.server.net_box_uri,
        params = {
            transport = 'ssl',
            ssl_ca_file = CA_FILE,
        },
    }
    local c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    c:close()
    -- Update the SSL certificate on the server.
    t.assert(fio.copyfile(SERVER2_CERT_FILE, TMP_CERT_FILE))
    t.assert(fio.copyfile(SERVER2_KEY_FILE, TMP_KEY_FILE))
    g.server:exec(function()
        box.cfg({listen = box.cfg.listen})
    end)
    -- Connection with the old CA should fail.
    c = net.connect(uri)
    t.assert_equals(c.state, 'error')
    c:close()
    -- Connection with the new CA should succeed.
    uri.params.ssl_ca_file = CA2_FILE
    c = net.connect(uri)
    t.assert_equals(c.state, 'active')
    c:close()
end

g.before_test('test_reload_replication', function()
    t.assert(fio.copyfile(CLIENT_CERT_FILE, TMP_CERT_FILE))
    t.assert(fio.copyfile(CLIENT_KEY_FILE, TMP_KEY_FILE))
    g.cluster = cluster:new({})
    g.master = g.cluster:build_and_add_server({alias = 'master'})
    g.master:start()
    g.master:exec(function(uri, ca_file, cert_file, key_file)
        box.cfg({
            listen = {
                uri = uri,
                params = {
                    transport = 'ssl',
                    ssl_ca_file = ca_file,
                    ssl_cert_file = cert_file,
                    ssl_key_file = key_file,
                },
            },
            replication_timeout = 0.1,
        })
    end, {g.master.net_box_uri, CA_FILE, SERVER_CERT_FILE, SERVER_KEY_FILE})
    g.replica = g.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {
                uri = g.master.net_box_uri,
                params = {
                    transport = 'ssl',
                    ssl_cert_file = TMP_CERT_FILE,
                    ssl_key_file = TMP_KEY_FILE,
                }
            },
            replication_timeout = 0.1,
            replication_connect_timeout = 0.5,
            replication_connect_quorum = 1,
            replication_sync_timeout = 300,
        }
    })
    g.replica:start()
end)

g.after_test('test_reload_replication', function()
    g.cluster:stop()
    g.cluster = nil
    g.master = nil
    g.replica = nil
    fio.unlink(TMP_CERT_FILE)
    fio.unlink(TMP_KEY_FILE)
end)

--
-- Checks that reconfiguring box.cfg.replication with the same URI triggers
-- reloading of SSL certificate files.
--
g.test_reload_replication = function()
    -- Update CA on the master.
    g.master:exec(function(ca_file)
        local listen = table.deepcopy(box.cfg.listen)
        listen.params.ssl_ca_file = ca_file
        box.cfg({listen = listen})
    end, {CA2_FILE})
    -- Try to restart replication on the replica with the same SSL certificate.
    -- It must fail because the certificate doesn't match the new CA.
    g.replica:exec(function()
        t.assert(box.info.replication[1].upstream)
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        local replication = box.cfg.replication
        box.cfg({replication = {}})
        box.cfg({replication = replication})
        t.assert_not(box.info.replication[1].upstream)
    end)
    -- Update the SSL certificate on the replica and check that it reconnects.
    t.assert(fio.copyfile(CLIENT2_CERT_FILE, TMP_CERT_FILE))
    t.assert(fio.copyfile(CLIENT2_KEY_FILE, TMP_KEY_FILE))
    g.replica:exec(function()
        box.cfg({replication = box.cfg.replication})
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
    -- Update CA on the master again.
    g.master:exec(function(ca_file)
        local listen = table.deepcopy(box.cfg.listen)
        listen.params.ssl_ca_file = ca_file
        box.cfg({listen = listen})
    end, {CA_FILE})
    -- Break the replica connection by temporarily setting a huge replication
    -- timeout on the master, which makes it stop sending heartbeats.
    -- Check that it fails to reconnect because its certificate doesn't match
    -- the new CA.
    g.master:exec(function()
        t.assert(box.info.replication[2].downstream)
        t.assert_equals(box.info.replication[2].downstream.status,
                            'follow')
        box.cfg({replication_timeout = 9000})
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[2].downstream)
            t.assert_not_equals(box.info.replication[2].downstream.status,
                                'follow')
        end)
        box.cfg({replication_timeout = 0.1})
    end)
    -- Update the SSL certificate on the replica and check that it reconnects.
    t.assert(fio.copyfile(CLIENT_CERT_FILE, TMP_CERT_FILE))
    t.assert(fio.copyfile(CLIENT_KEY_FILE, TMP_KEY_FILE))
    g.replica:exec(function()
        t.assert(box.info.replication[1].upstream)
        t.assert_not_equals(box.info.replication[1].upstream.status, 'follow')
        box.cfg({replication = box.cfg.replication})
        t.helpers.retrying({}, function()
            t.assert(box.info.replication[1].upstream)
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
end

g.before_test('test_console', function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function(uri, cert_file, key_file)
        box.cfg({listen = {
            uri = uri,
            params = {
                transport = 'ssl',
                ssl_cert_file = cert_file,
                ssl_key_file = key_file,
            }
        }})
    end, {cg.server.net_box_uri, SERVER_CERT_FILE, SERVER_KEY_FILE})
end)

g.after_test('test_console', function(cg)
    cg.server:drop()
    cg.server = nil
end)

--
-- Checks that the interactive console works over the binary protocol with
-- enabled SSL (gh-567).
--
g.test_console = function(cg)
    local client = it:new()
    client:execute_command(string.format(
        [[require('console').connect('%s?transport=ssl')]],
        cg.server.net_box_uri))
    t.assert_equals(client:read_response(), true)
    client:close()
end

local function assert_no_sensitive_ssl_data_in_cfg_option(server, option,
                                                          sensitive_data)
    local set_option_log = server:grep_log(
        string.format("set '%s' configuration option to (.*)", option))
    t.assert(set_option_log)
    for ssl_key, ssl_value in pairs(sensitive_data) do
        t.assert_not(string.match(set_option_log, ssl_key))
        t.assert_not(string.match(set_option_log, ssl_value))
    end
end

local function server_cfg_with_security_checks(server, listen_cfg_option)
    server:exec(function(listen_cfg_option)
        box.cfg({listen = listen_cfg_option})
    end, {listen_cfg_option})
    assert_no_sensitive_ssl_data_in_cfg_option(server, "listen",
                                               SENSITIVE_SSL_DATA)
end

g.before_test('test_no_ssl_data_in_single_string_uri', function()
    g.ssl_params = table.deepcopy(SENSITIVE_SSL_DATA)
    g.ssl_params.transport = "ssl"
    g.server = server:new({alias = "server"})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
    g.server = nil
end)

g.test_no_ssl_data_in_single_string_uri = function()
    local listen_cfg_option = SENSITIVE_URI
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_multiple_string_uris = function()
    local listen_cfg_option = SENSITIVE_URI .. ', ' .. SENSITIVE_URI
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_single_string_uri_table = function()
    local listen_cfg_option = { SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_uri_field = function()
    local listen_cfg_option = { uri = SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_params_field = function()
    local listen_cfg_option = { SAFE_URI, params = g.ssl_params }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_uri_and_params_fields = function()
    local listen_cfg_option = { uri = SAFE_URI, params = g.ssl_params }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_several_string_uris = function()
    local listen_cfg_option = { SENSITIVE_URI, SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_and_single_string_uri = function()
    local listen_cfg_option = { { SENSITIVE_URI }, SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_uri_field_and_single_uri = function()
    local listen_cfg_option = { { uri = SENSITIVE_URI }, SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_params_field_and_single_uri = function()
    local listen_cfg_option = {{ SAFE_URI, params = g.ssl_params },
                               SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_table_with_uri_params_fields_and_single_uri = function()
    local listen_cfg_option = {{ uri = SAFE_URI, params = g.ssl_params },
                               SENSITIVE_URI }
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_several_table_with_uri_field = function()
    local listen_cfg_option = {{ uri = SENSITIVE_URI },
                               { uri = SENSITIVE_URI }}
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_several_table_with_params_field = function()
    local listen_cfg_option = {{ SAFE_URI, params = g.ssl_params },
                               { SAFE_URI, params = g.ssl_params }}
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

g.test_no_ssl_data_in_several_table_with_uri_and_params_field = function()
    local listen_cfg_option = {{ uri = SAFE_URI, params = g.ssl_params },
                               { uri = SAFE_URI, params = g.ssl_params }}
    server_cfg_with_security_checks(g.server, listen_cfg_option)
end

local g_repl = t.group("ssl_replication")

g_repl.before_all(function()
    g_repl.cluster = cluster:new({})
    g_repl.master = g_repl.cluster:build_and_add_server({alias = 'master'})
    g_repl.master:start()
    g_repl.master:exec(function(uri, ssl_params)
        ssl_params.transport = "ssl"
        box.cfg({
            listen = {
                uri = uri,
                params = ssl_params,
            },
            replication_timeout = 0.1,
        })
    end, {g_repl.master.net_box_uri, SENSITIVE_SSL_DATA})

    local ssl_params = table.copy(SENSITIVE_SSL_DATA)
    ssl_params.transport = "ssl"
    g_repl.replica = g_repl.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {
                uri = g_repl.master.net_box_uri,
                params = ssl_params
            },
            replication_timeout = 0.1,
            replication_connect_timeout = 0.3,
        }
    })
    g_repl.replica:start()
end)

g_repl.after_all(function()
    g_repl.cluster:drop()
end)

g_repl.test_no_sensitive_ssl_data_in_replication_option = function()
    assert_no_sensitive_ssl_data_in_cfg_option(g_repl.replica, "replication",
                                               SENSITIVE_SSL_DATA)
end

g_repl.test_no_sensitive_ssl_data_in_applier_fiber_name = function()
    -- Of all the logs, we are looking for those whose fiber name has
    -- the name "applier".
    local logged_applier_uri = g_repl.replica:grep_log(".*applier/(.-)%s.*")
    t.assert(logged_applier_uri)
    -- If we find this log, we should extract main uri body and additional
    -- uri params from it.
    local uri, params = string.match(logged_applier_uri, "^(.-)%?(.*)$")
    t.assert(uri)
    t.assert(params)
    -- If additional params presence in uri, we extract each "name=value"
    -- pair from them and check that there is no sensitive ssl data in it.
    for key, _ in string.gmatch(params, "([^&=]+)=([^&=]*)") do
        t.assert_not(SENSITIVE_SSL_DATA[key])
    end
end
