#
# Copyright (C) 2009, 2010 Mail.RU
# Copyright (C) 2009, 2010 Yuriy Vostrikov
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

require 'socket'

IPROTO_PING = 0xff00

include Socket::Constants

class IProtoError < RuntimeError
end

class IProto
  @@sync = 0

  def initialize(server, param = {})
    host, port = server.split(/:/)
    @end_point = [host, port.to_i]
    [:logger, :reconnect].each do |p|
      instance_variable_set "@#{p}", param[p] if param.has_key? p
    end

    reconnect
  end

  attr_reader :sock

  def hexdump(string)
    string.unpack('C*').map{ |c| "%02x" % c }.join(' ')
  end

  def next_sync
    @@sync += 1
    if @@sync > 0xffffffff
      @@sync = 0
    end
    @@sync
  end

  def reconnect
    @sock = TCPSocket.new(*@end_point)
    @sock.setsockopt(Socket::IPPROTO_TCP, Socket::TCP_NODELAY, true)
  end

  def close
    @sock.close unless @sock.closed
  end

  def send(message)
    begin
      reconnect if @sock.closed? and @reconnect

      sync = self.next_sync
      payload = message[:raw] || message[:data].pack(message[:pack] || 'L*')

      buf = [message[:code], payload.bytesize, sync].pack('L3')
      @logger.debug { "#{@end_point} => send hdr #{buf.unpack('L*').map{ |c| "%010i" % c }.join(' ')}" } if @logger

      buf << payload
      @logger.debug { "#{@end_point} => send bdy #{hexdump(payload)}" } if @logger

      @sock.write(buf)

      header = @sock.read(12)
      raise IProtoError, "can't read header" unless header
      header = header.unpack('L3')
      @logger.debug { "#{@end_point} => recv hdr #{header.map{ |c| "%010i" % c }.join(' ')}" } if @logger

      raise IProtoError, "response:#{header[0]} != message:#{message[:code]}" if header[0] != message[:code]
      raise IProtoError, "response:#{header[2]} != message:#{sync}" if header[2] != sync

      data = @sock.read(header[1])
      @logger.debug { "#{@end_point} => recv bdy #{hexdump(data)}" } if @logger
      data
    rescue Exception => exc
      @sock.close
      raise exc
    end
  end

  def msg(message)
    reply = send message
    result = pre_process_reply message, reply

    return yield result if block_given?
    result
  end

  def ping
    send :code => IPROTO_PING, :raw => ''
    :pong
  end

  def pre_process_reply(message, data)
    if message[:unpack]
      data.unpack(message[:unpack])
    else
      data
    end
  end
end

class IProtoRetCode < IProto
  def pre_process_reply(message, data)
    raise IProtoError, "too small response" if data.nil? or data.bytesize < 4
    ret_code = data.slice!(0, 4).unpack('L')[0]
    raise IProtoError, "remote: #{'0x%x' % ret_code}" if ret_code != 0
    super message, data
  end
end
