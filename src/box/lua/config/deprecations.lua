local textutils = require('internal.config.utils.textutils')

local format_text = textutils.format_text

local I = {}
local C = {}

-- {{{ Instance deprecations

local iproto_ssl_params_deprecation = {
    since = {'3.3.4', '3.4.2', '3.5.1'},
    see = 'iproto.ssl',
    msg = format_text([[
        These SSL options are used both for the server and for all of the
        clients which makes them unsuitable to configure different keys and
        certificates for the peers to use for connecting.
    ]])
}

I['iproto.listen.*.params.ssl_ca_file'] = iproto_ssl_params_deprecation
I['iproto.listen.*.params.ssl_key_file'] = iproto_ssl_params_deprecation
I['iproto.listen.*.params.ssl_cert_file'] = iproto_ssl_params_deprecation
I['iproto.listen.*.params.ssl_ciphers'] = iproto_ssl_params_deprecation
I['iproto.listen.*.params.ssl_password'] = iproto_ssl_params_deprecation
I['iproto.listen.*.params.ssl_password_file'] = iproto_ssl_params_deprecation

-- }}} Instance deprecations

-- {{{ Cluster deprecations

-- }}} Cluster deprecations

return {
    instance_deprecations = I,
    cluster_deprecations = C,
}
