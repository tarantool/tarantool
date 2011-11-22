package tarantool.connector.socketpool.worker;

import java.io.IOException;
import java.net.InetAddress;

import tarantool.connector.socketpool.AbstractSocketPool;

public enum FactoryType {
    PLAIN_SOCKET {
        @Override
        public SocketFactory createFactory(final InetAddress address, final int port, final int soTimeout, final AbstractSocketPool pool) {
            return new SocketFactory() {
                @Override
                public SocketWorkerInternal create() throws IOException {
                    return new PlainSocketWorker(address, port, soTimeout, pool);
                }
            };
        }
    },

    CHANNEL_SOCKET {
        @Override
        public SocketFactory createFactory(final InetAddress address, final int port, final int soTimeout, final AbstractSocketPool pool) {
            return new SocketFactory() {
                @Override
                public SocketWorkerInternal create() throws IOException {
                    return new ChannelSocketWorker(address, port,soTimeout, pool);
                }
            };
        }
    };

    public abstract SocketFactory createFactory(InetAddress address, int port, int soTimeout, AbstractSocketPool pool);
}
