import socket
import struct
import sys
import re
from tarantool_connection import TarantoolConnection

MEMCACHED_SEPARATOR = '\r\n'

class MemcachedCommandBuffer:

    def __init__(self, commands):
        self.buf = commands

    def read_line(self):
        if self.buf == None:
            return None

        index = self.buf.find(MEMCACHED_SEPARATOR)
        if index > 0:
            line = self.buf[:index]
            self.buf = self.buf[index + 2:]
        else:
            line = self.buf
            self.buf = None
        return line

class MemcachedConnection(TarantoolConnection):

    def execute_no_reconnect(self, commands, silent = True):
        self.send(commands, silent)
        return self.recv(silent)

    def send(self, commands, silent = True):
        self.commands = commands
        self.socket.sendall(commands)
        if not silent:
            sys.stdout.write(self.commands)

    def recv(self, silent = True):
        self.recv_buffer = ''
        self.command_buffer = MemcachedCommandBuffer(self.commands)
        self.reply = ''

        while True:
            cmd = self.command_buffer.read_line()
            if cmd == None:
                # end of buffer reached
                break

            if re.match('set|add|replace|append|prepend|cas', cmd, re.I):
                self.reply_storage(cmd)
            elif re.match('get|gets', cmd, re.I):
                self.reply_retrieval(cmd)
            elif re.match('delete', cmd, re.I):
                self.reply_deletion(cmd)
            elif re.match('incr|decr', cmd, re.I):
                self.reply_incr_decr(cmd)
            elif re.match('stats', cmd, re.I):
                self.reply_stats(cmd)
            elif re.match('flush_all|version|quit', cmd, re.I):
                self.reply_other(cmd)
            elif cmd == '':
                continue
            else:
                self.reply_unknown(cmd)

        if not silent:
            sys.stdout.write(self.reply)

        return self.reply

    def reply_storage(self, cmd):
        self.command_buffer.read_line()
        self.reply_single_line(cmd)

    def reply_retrieval(self, cmd):
        while True:
            # read reply cmd
            key = self.read_line()
            # store line in reply buffer
            self.reply += key + MEMCACHED_SEPARATOR

            # chec reply type
            if re.match('VALUE', key):
                # Value header received
                key_params = key.split()
                if len(key_params) < 4:
                    continue

                # receive value
                value_len = int(key_params[3])
                while value_len > 0:
                    # Receive value line
                    value = self.read_line()
                    # store value line in reply buffer
                    self.reply += value + MEMCACHED_SEPARATOR
                    # decrease value len
                    value_len -= len(value)
            elif re.match('END', key):
                break
            elif re.match('ERROR|CLIENT_ERROR|SERVER_ERROR', key):
                break
            else:
                # unknown
                print "error: unknown line: '%s'" % key
                self.reply += "error: unknown line: '%s'" % key
                break

    def reply_deletion(self, cmd):
        self.reply_single_line(cmd)

    def reply_incr_decr(self, cmd):
        self.reply_single_line(cmd)

    def reply_stats(self, cmd):
        while True:
            # read reply stats
            stat = self.read_line()
            # store stat in reply buffer
            self.reply += stat + MEMCACHED_SEPARATOR

            if re.match('END', stat):
                break

            if re.match('ERROR|CLIENT_ERROR|SERVER_ERROR', stat):
                break

    def reply_other(self, cmd):
        self.reply_single_line(cmd)

    def reply_single_line(self, cmd):
        params = cmd.split()
        if re.match('noreply', params[-1], re.I):
            # Noreply option exist
            noreply = True
        else:
            noreply = False

        if not noreply:
            self.reply += self.read_line() + MEMCACHED_SEPARATOR

    def reply_unknown(self, line):
        reply = self.read_line()
        self.reply += reply + MEMCACHED_SEPARATOR

    def read_line(self):
        buf = self.recv_buffer
        while True:
            # try to find separator in the exist buffer
            index = buf.find(MEMCACHED_SEPARATOR)
            if index > 0:
                break
            data = self.socket.recv(4096)
            if not data:
                return None
            buf += data
        # get line
        line = buf[:index]
        # cut line from receive buffer
        self.recv_buffer = buf[index + 2:]
        return line

