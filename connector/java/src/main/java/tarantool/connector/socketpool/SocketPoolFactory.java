package tarantool.connector.socketpool;

import java.net.UnknownHostException;

import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;


public class SocketPoolFactory {

    public static SocketPool createSocketPool(SocketPoolConfig socketPoolConfig)
            throws UnknownHostException, SocketPoolTimeOutException {
        switch (socketPoolConfig.getSocketPoolType()) {
            case DYNAMIC_POOL:
                return new DynamicSocketPool(socketPoolConfig);
            case STATIC_POOL:
                return new StaticSocketPool(socketPoolConfig);
            default:
                throw new IllegalArgumentException("Incorrect socket pool type: " + socketPoolConfig.getSocketPoolType());
        }
    }
}
