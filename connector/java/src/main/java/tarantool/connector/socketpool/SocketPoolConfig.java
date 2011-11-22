package tarantool.connector.socketpool;

import tarantool.connector.socketpool.worker.FactoryType;

public class SocketPoolConfig {

    private String host;
    private int port;
    private int socketReadTimeout;
    private int minPoolSize;
    private int maxPoolSize;
    private long waitingTimeout;
    private long reconnectTimeout;
    private long initializeTimeout;
    private int disconnectBound;
    private FactoryType type;
    private SocketPoolType socketPoolType;
    private long latencyPeriod;

    public SocketPoolConfig(String host, int port, int socketReadTimeout, int minPoolSize, int maxPoolSize, long waitingTimeout,
            long reconnectTimeout, long initializeTimeout, int disconnectBound, FactoryType type, SocketPoolType socketPoolType,
            long latencyPeriod) {
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

    public SocketPoolConfig(String host, int port, int socketReadTimeout, int minPoolSize, long waitingTimeout, long reconnectTimeout,
            long initializeTimeout, int disconnectBound, FactoryType type, SocketPoolType socketPoolType) {
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

    public String getHost() {
        return host;
    }

    public int getPort() {
        return port;
    }
    
    public int getSocketReadTimeout() {
        return socketReadTimeout;
    }

    public int getMinPoolSize() {
        return minPoolSize;
    }

    public int getMaxPoolSize() {
        return maxPoolSize;
    }

    public long getWaitingTimeout() {
        return waitingTimeout;
    }

    public long getReconnectTimeout() {
        return reconnectTimeout;
    }

    public long getInitializeTimeout() {
        return initializeTimeout;
    }

    public int getDisconnectBound() {
        return disconnectBound;
    }

    public FactoryType getType() {
        return type;
    }

    public long getLatencyPeriod() {
        return latencyPeriod;
    }

    public SocketPoolType getSocketPoolType() {
        return socketPoolType;
    }

    @Override
    public String toString() {
        return "SocketPoolConfig{" +
                "host='" + host + '\'' +
                ", port=" + port +
                ", socketReadTimeout=" + socketReadTimeout +
                ", minPoolSize=" + minPoolSize +
                ", maxPoolSize=" + maxPoolSize +
                ", waitingTimeout=" + waitingTimeout +
                ", reconnectTimeout=" + reconnectTimeout +
                ", initializeTimeout=" + initializeTimeout +
                ", disconnectBound=" + disconnectBound +
                ", type=" + type +
                ", socketPoolType=" + socketPoolType +
                ", latencyPeriod=" + latencyPeriod +
                '}';
    }
}
