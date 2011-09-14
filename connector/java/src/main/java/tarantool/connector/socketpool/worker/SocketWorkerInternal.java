package tarantool.connector.socketpool.worker;

import java.io.IOException;
import java.net.InetAddress;

import tarantool.connector.socketpool.AbstractSocketPool;


public abstract class SocketWorkerInternal implements SocketWorker, Comparable<SocketWorkerInternal> {
    //TODO: Clarify Dynamic or Static SocketWorker for best performance in Static Socket Pool

    private enum ConnectionState {
        CONNECTED,
        DISCONNECTED;
    }

    private long lastTimeStamp;
    private final AbstractSocketPool pool;

    final InetAddress address;
    final int port;
    final int soTimeout;

    ConnectionState state = ConnectionState.DISCONNECTED;

    SocketWorkerInternal(AbstractSocketPool pool, InetAddress address, int port, int soTimeout) {
        this.pool = pool;
        this.address = address;
        this.port = port;
        this.soTimeout = soTimeout;

        lastTimeStamp = System.currentTimeMillis();
    }

    final void connected() {
        state = ConnectionState.CONNECTED;
    }

    final void disconnected() {
        state = ConnectionState.DISCONNECTED;
    }

    public final boolean isConnected() {
        return state == ConnectionState.CONNECTED;
    }

    public long getLastTimeStamp() {
        return lastTimeStamp;
    }

    @Override
    public int compareTo(SocketWorkerInternal o) {
        long result = lastTimeStamp - o.lastTimeStamp;
        return result == 0 ? 0 : result > 0 ? 1 : -1;
    }

    @Override
    public final void release() {
        lastTimeStamp = System.currentTimeMillis();
        pool.returnSocketWorker(this);
    }

    public abstract void close();
    public abstract void connect() throws IOException;
}
