package tarantool.connector.socketpool;

import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.worker.SocketWorker;

public interface SocketPool {
    SocketWorker borrowSocketWorker() throws InterruptedException,
            SocketPoolException;

    void close();
}
