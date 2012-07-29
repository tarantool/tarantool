package tarantool.connector.socketpool;

import java.io.IOException;
import java.net.UnknownHostException;
import java.util.Deque;
import java.util.LinkedList;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;


import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.Log;

import tarantool.connector.socketpool.exception.SocketPoolClosedException;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.exception.SocketPoolUnavailableException;
import tarantool.connector.socketpool.worker.SocketWorker;
import tarantool.connector.socketpool.worker.SocketWorkerInternal;

/**
 * Socket pool with low and top waterline. Socket pool uses dynamic balancer for
 * thread count
 */
class DynamicSocketPool extends AbstractSocketPool {

    private static final Log LOG = LogFactory.getLog(DynamicSocketPool.class);

    private static final long MIN_LATENCY_PERIOD = 5000; // 5 sec


    private int currentUsed = 0;
    private final long latency;
    private final Lock lock = new ReentrantLock();
    private final Condition cond = lock.newCondition();

    private final int maxPoolSize;

    private final int minPoolSize;
    private final Deque<SocketWorkerInternal> queue = new LinkedList<SocketWorkerInternal>();

    private final ScheduledExecutorService scheduler = Executors
            .newSingleThreadScheduledExecutor(new ThreadFactory() {
                @Override
                public Thread newThread(Runnable r) {
                    final Thread thread = new Thread(r,
                            "DynamicSocketPoolLatencyCleaner");
                    thread.setDaemon(true);
                    thread.setPriority(Thread.MIN_PRIORITY);
                    return thread;
                }
            });

    public DynamicSocketPool(SocketPoolConfig config)
            throws UnknownHostException, SocketPoolTimeOutException {
        super(config.getHost(), config.getPort(),
                config.getSocketReadTimeout(), config.getWaitingTimeout(),
                config.getReconnectTimeout(), config.getInitializeTimeout(),
                config.getDisconnectBound(), config.getType());

        if (config.getMinPoolSize() < 0
                || config.getMaxPoolSize() > config.getMaxPoolSize()
                || config.getMaxPoolSize() == 0) {
            throw new IllegalArgumentException(
                    "Incorrect value of min or max pool size");
        }

        if (config.getLatencyPeriod() < 0) {
            throw new IllegalArgumentException(
                    "Incorrect value of latency time");
        }

        this.minPoolSize = config.getMinPoolSize();
        this.maxPoolSize = config.getMaxPoolSize();
        this.latency = config.getLatencyPeriod();

        startSocketPoolCleaner();

        initializePool();
    }

    @Override
    public SocketWorker borrowSocketWorker() throws InterruptedException,
            SocketPoolException {
        SocketWorkerInternal socketWorker;

        if (stateMachine.isClosed()) {
            throw new SocketPoolClosedException(
                    "Socket pool is closed, borrowing of socket worker was rejected");
        }

        if (stateMachine.isReconnecting()) {
            throw new SocketPoolUnavailableException(
                    "Socket pool is reconnecting, borrowing of socket worker was rejected");
        }

        lock.lock();
        try {
            do {
                socketWorker = queue.pollFirst();
                if (socketWorker == null) {
                    if (currentUsed < maxPoolSize) {
                        try {
                            socketWorker = socketWorkerFactory.create();
                        } catch (IOException e) {
                            LOG.warn("Can't establish socket connection because: Exception - "
                                    + e.getClass().getSimpleName()
                                    + " and case - " + e.getMessage());
                            throw new SocketPoolUnavailableException(
                                    "Can't create extra socket", e);
                        }
                    } else {
                        while (queue.isEmpty()) {
                            if (!cond.await(waitingTimeout,
                                    TimeUnit.MILLISECONDS)) {
                                throw new SocketPoolTimeOutException(
                                        "Timeout is occurred while wait for socket worker");
                            }
                        }

                        socketWorker = queue.pollFirst();
                        assert socketWorker != null : "Incorrect state of queue";
                    }
                }
            } while (socketWorker == null);

            currentUsed++;

        } finally {
            lock.unlock();
        }

        return socketWorker;
    }

    @Override
    void feedReconnect() {
        lock.lock();
        try {
            for (final SocketWorkerInternal worker : queue) {
                pushToReconnect(worker);
            }
            queue.clear();
        } finally {
            lock.unlock();
        }
    }

    private void initializePool() throws SocketPoolTimeOutException {
        final long startTime = System.currentTimeMillis();

        for (int i = 0; i < minPoolSize;) {

            if (stateMachine.isClosed()) {
                LOG.info("Socket pool is closed, initialization aborted");
                return; // Socket pool is closed
            }

            lock.lock();
            try {
                if (queue.size() + currentUsed > minPoolSize) {
                    break;
                }

                try {
                    final SocketWorkerInternal socketWorker = socketWorkerFactory
                            .create();
                    queue.addFirst(socketWorker);
                    i++;
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
            } finally {
                lock.unlock();
            }

            if (System.currentTimeMillis() - startTime > initializeTimeout) {
                throw new SocketPoolTimeOutException(
                        "Initialize pool timeout occur");
            }
        }
    }

    @Override
    void internalClose() {
        LOG.info("Socket pool internal close ...");

        lock.lock();
        try {
            scheduler.shutdownNow();
            for (final SocketWorkerInternal socketWorker : queue) {
                socketWorker.close();
            }
        } finally {
            lock.unlock();
        }
    }

    @Override
    public void internalReturnSocketWorker(SocketWorkerInternal socketWorker) {

        if (stateMachine.isClosed()) {
            socketWorker.close();
            LOG.info("Socket pool is closed, return operation skipped");
            return;
        }

        lock.lock();
        try {
            if (currentUsed > 0) {
                currentUsed--;
            }

            queue.addFirst(socketWorker);

            cond.signal();
        } finally {
            lock.unlock();
        }
    }

    private void startSocketPoolCleaner() {
        final Runnable cleanTask = new Runnable() {
            @Override
            public void run() {
                long scheduleTime = latency;

                if (stateMachine.isClosed()) {
                    LOG.info("Socket pool is closed, cleaner thread stopped");
                    return;
                }

                lock.lock();
                try {
                    if (queue.size() + currentUsed <= minPoolSize) {
                        return;
                    }

                    long remainingTime;
                    final long currentTime = System.currentTimeMillis();
                    while (!queue.isEmpty()) {
                        final SocketWorkerInternal socketWorker = queue
                                .peekLast();
                        assert socketWorker != null : "Deque must contain socket worker";

                        remainingTime = currentTime
                                - socketWorker.getLastTimeStamp();
                        if (remainingTime > latency) {
                            final SocketWorkerInternal removed = queue
                                    .pollLast();
                            assert removed == socketWorker : "Incorrect operation peek and remove";

                            socketWorker.close();
                        } else {
                            scheduleTime = latency - remainingTime;
                            break;
                        }
                    }
                } finally {
                    lock.unlock();

                    if (!stateMachine.isClosed()) {
                        scheduler.schedule(this, MIN_LATENCY_PERIOD
                                + scheduleTime, TimeUnit.MILLISECONDS);
                    }
                }
            }
        };

        if (!stateMachine.isClosed()) {
            scheduler.schedule(cleanTask, MIN_LATENCY_PERIOD + latency,
                    TimeUnit.MILLISECONDS);
        }
    }
}