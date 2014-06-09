remote = require 'net.box'

port = box.cfg.primary_port

cn = remote:new('localhost', port)
cn

cn.proto.ping(0)
