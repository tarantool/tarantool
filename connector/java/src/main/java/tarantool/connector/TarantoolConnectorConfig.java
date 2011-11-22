package tarantool.connector;

import tarantool.connector.socketpool.SocketPoolConfig;
import tarantool.connector.socketpool.SocketPoolType;
import tarantool.connector.socketpool.worker.FactoryType;


public class TarantoolConnectorConfig {

    private final SocketPoolConfig socketPoolConfig;
    private final int nameSpace;

    public TarantoolConnectorConfig(String host, int port, int socketReadTimeout, int minPoolSize, int maxPoolSize, long waitingTimeout,
            long reconnectTimeout, long initializeTimeout, int disconnectBound, FactoryType type, SocketPoolType socketPoolType,
            long latencyPeriod,  int nameSpace) {
        this.socketPoolConfig = new SocketPoolConfig(host, port, socketReadTimeout, minPoolSize, maxPoolSize, waitingTimeout, reconnectTimeout,
                initializeTimeout, disconnectBound, type, socketPoolType, latencyPeriod);
        this.nameSpace = nameSpace;
    }

    public int getNameSpace() {
        return nameSpace;
    }

    public SocketPoolConfig getSocketPoolConfig() {
        return socketPoolConfig;
    }

    @Override
    public String toString() {
        return "TarantoolConnectorConfig{" +
                "socketPoolConfig=" + socketPoolConfig +
                ", nameSpace=" + nameSpace +
                '}';
    }
}
