-- normalize_uri.lua - internal file

box.internal.cfg_get_listen_type = function() return 'string, number' end
box.internal.cfg_get_listen = function() return box.cfg.listen end
