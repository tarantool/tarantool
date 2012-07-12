package tarantool.connector.socketpool;

import tarantool.connector.socketpool.worker.FactoryType;

public class SocketPoolConfig {

    private final int disconnectBound;
    private final String host;
    private final long initializeTimeout;
    private long latencyPeriod;
    private int maxPoolSize;
    private final int minPoolSize;
    private final int port;
    private final long reconnectTimeout;
    private final SocketPoolType socketPoolType;
    private final int socketReadTimeout;
    private final FactoryType type;
    private final long waitingTimeout;

    public SocketPoolConfig(String host, int port, int socketReadTimeout,
            int minPoolSize, int maxPoolSize, long waitingTimeout,
            long reconnectTimeout, long initializeTimeout, int disconnectBound,
            FactoryType type, SocketPoolType socketPoolType, long latencyPeriod) {
        this.host = host;
        this.port = port;
        this.socketReadTimeout = socketReadTimeout;
        this.minPoolSize = minPoolSize;
        this.maxPoolSize = maxPoolSize;
        this.waitingTimeout = waitingTimeout;
        this.reconnectTimeout = reconnectTimeout;
        this.initializeTimeout = initializeTimeout;
        this.disconnectBound = disconnectBound;
        this.type = type;
        this.socketPoolType = socketPoolType;
        this.latencyPeriod = latencyPeriod;
    }

    public SocketPoolConfig(String host, int port, int socketReadTimeout,
            int minPoolSize, long waitingTimeout, long reconnectTimeout,
            long initializeTimeout, int disconnectBound, FactoryType type,
            SocketPoolType socketPoolType) {
        this.host = host;
        this.port = port;
        this.socketReadTimeout = socketReadTimeout;
        this.minPoolSize = minPoolSize;
        this.waitingTimeout = waitingTimeout;
        this.reconnectTimeout = reconnectTimeout;
        this.initializeTimeout = initializeTimeout;
        this.disconnectBound = disconnectBound;
        this.type = type;
        this.socketPoolType = socketPoolType;
    }

    public int getDisconnectBound() {
        return disconnectBound;
    }

    public String getHost() {
        return host;
    }

    public long getInitializeTimeout() {
        return initializeTimeout;
    }

    public long getLatencyPeriod() {
        return latencyPeriod;
    }

    public int getMaxPoolSize() {
        return maxPoolSize;
    }

    public int getMinPoolSize() {
        return minPoolSize;
    }

    public int getPort() {
        return port;
    }

    public long getReconnectTimeout() {
        return reconnectTimeout;
    }

    public SocketPoolType getSocketPoolType() {
        return socketPoolType;
    }

    public int getSocketReadTimeout() {
        return socketReadTimeout;
    }

    public FactoryType getType() {
        return type;
    }

    public long getWaitingTimeout() {
        return waitingTimeout;
    }

    @Override
    public String toString() {
        return "SocketPoolConfig{" + "host='" + host + '\'' + ", port=" + port
                + ", socketReadTimeout=" + socketReadTimeout + ", minPoolSize="
                + minPoolSize + ", maxPoolSize=" + maxPoolSize
                + ", waitingTimeout=" + waitingTimeout + ", reconnectTimeout="
                + reconnectTimeout + ", initializeTimeout=" + initializeTimeout
                + ", disconnectBound=" + disconnectBound + ", type=" + type
                + ", socketPoolType=" + socketPoolType + ", latencyPeriod="
                + latencyPeriod + '}';
    }
}
