package tarantool.connector.socketpool;

import java.io.IOException;
import java.net.UnknownHostException;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.Log;

import tarantool.connector.socketpool.exception.SocketPoolClosedException;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.exception.SocketPoolUnavailableException;
import tarantool.connector.socketpool.worker.SocketWorker;
import tarantool.connector.socketpool.worker.SocketWorkerInternal;

class StaticSocketPool extends AbstractSocketPool {

    private static final Log LOG = LogFactory.getLog(StaticSocketPool.class);

    private final int poolSize;

    private final BlockingQueue<SocketWorkerInternal> queue = new LinkedBlockingQueue<SocketWorkerInternal>();

    private final ReadWriteLock rwLock = new ReentrantReadWriteLock();

    public StaticSocketPool(SocketPoolConfig config)
            throws UnknownHostException, SocketPoolTimeOutException {
        super(config.getHost(), config.getPort(),
                config.getSocketReadTimeout(), config.getWaitingTimeout(),
                config.getReconnectTimeout(), config.getInitializeTimeout(),
                config.getDisconnectBound(), config.getType());

        if (config.getMinPoolSize() <= 0) {
            throw new IllegalArgumentException("Incorrect value of pool size");
        }

        this.poolSize = config.getMinPoolSize();

        initializePool();
    }

    @Override
    public SocketWorker borrowSocketWorker() throws InterruptedException,
            SocketPoolException {

        if (stateMachine.isReconnecting()) {
            throw new SocketPoolUnavailableException(
                    "Socket pool is reconnecting, borrowing of socket worker was rejected");
        }

        final SocketWorker worker = queue.poll(waitingTimeout,
                TimeUnit.MILLISECONDS);

        if (stateMachine.isClosed()) {
            throw new SocketPoolClosedException(
                    "Socket pool is closed, borrowing of socket worker was rejected");
        }

        if (worker == null) {
            throw new SocketPoolTimeOutException(
                    "Timeout is occurred while wait for socket worker");
        }

        return worker;
    }

    @Override
    void feedReconnect() {
        rwLock.writeLock().lock();
        try {
            for (final SocketWorkerInternal worker : queue) {
                pushToReconnect(worker);
            }
            queue.clear();
        } finally {
            rwLock.writeLock().unlock();
        }
    }

    private void initializePool() throws SocketPoolTimeOutException {
        final long startTime = System.currentTimeMillis();

        for (int i = 0; i < poolSize;) {
            try {
                final SocketWorkerInternal socketWorker = socketWorkerFactory
                        .create();
                queue.add(socketWorker);
                i++;

                if (stateMachine.isClosed()) {
                    socketWorker.close();
                    LOG.info("Socket pool is closed, initialization aborted");
                    break;
                }
            } catch (IOException e) {
                LOG.warn("Can't establish socket connection because: Exception - "
                        + e.getClass().getSimpleName()
                        + " and case - "
                        + e.getMessage());

                try {
                    Thread.sleep(reconnectTimeout);
                } catch (InterruptedException e1) {
                    LOG.error("Thread in reconnect timeout state is interrupted. Reconnection is aborted");
                    Thread.currentThread().interrupt();
                    return;
                }
            }

            if (System.currentTimeMillis() - startTime > initializeTimeout) {
                throw new SocketPoolTimeOutException(
                        "Initialize pool timeout occur");
            }
        }
    }

    @Override
    void internalClose() {
        for (final SocketWorkerInternal socketWorker : queue) {
            socketWorker.close();
        }
    }

    @Override
    public void internalReturnSocketWorker(SocketWorkerInternal socketWorker) {
        rwLock.readLock().lock();
        try {
            final boolean added = queue.offer(socketWorker);
            assert added : "Queue can't add wrapper, too many socket worker for queue size";

            if (stateMachine.isClosed()) {
                socketWorker.close();
                LOG.info("Socket pool is closed, return operation skipped");
            }
        } finally {
            rwLock.readLock().unlock();
        }
    }
}
