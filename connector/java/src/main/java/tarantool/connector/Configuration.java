/*
 * Copyright (C) 2012 Mail.RU
 * Copyright (C) 2012 Eugine Blikh
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

package tarantool.connector;

import tarantool.connector.socketpool.SocketPoolConfig;
import tarantool.connector.socketpool.SocketPoolType;
import tarantool.connector.socketpool.worker.FactoryType;

public class Configuration {

    private final SocketPoolConfig socketPoolConfig;

    public Configuration(String host, int port, int socketReadTimeout,
            int minPoolSize, int maxPoolSize, long waitingTimeout,
            long reconnectTimeout, long initializeTimeout, int disconnectBound,
            FactoryType type, SocketPoolType socketPoolType, long latencyPeriod) {
        this.socketPoolConfig = new SocketPoolConfig(host, port,
                socketReadTimeout, minPoolSize, maxPoolSize, waitingTimeout,
                reconnectTimeout, initializeTimeout, disconnectBound, type,
                socketPoolType, latencyPeriod);
    }

    public SocketPoolConfig getSocketPoolConfig() {
        return socketPoolConfig;
    }

    @Override
    public String toString() {
        return "TarantoolConnectorConfig{" + "socketPoolConfig="
                + socketPoolConfig + '}';
    }
}
