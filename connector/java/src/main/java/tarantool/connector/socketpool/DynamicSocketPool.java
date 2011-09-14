package tarantool.connector.socketpool;

import java.io.IOException;
import java.net.UnknownHostException;
import java.util.PriorityQueue;
import java.util.Queue;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import tarantool.connector.socketpool.exception.SocketPoolClosedException;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.exception.SocketPoolUnavailableException;
import tarantool.connector.socketpool.worker.SocketWorker;
import tarantool.connector.socketpool.worker.SocketWorkerInternal;

/**
 * Socket pool with low and top waterline. Socket pool uses dynamic balancer for thread count
 */
class DynamicSocketPool extends AbstractSocketPool {

    private static final Log LOG = LogFactory.getLog(DynamicSocketPool.class);

    private static final long MIN_LATENCY_PERIOD = 5000;        //5 sec

    private final long latency;
    private final int minPoolSize;
    private final int maxPoolSize;
	private int currentUsed = 0;

    private final Queue<SocketWorkerInternal> queue = new PriorityQueue<SocketWorkerInternal>();

    private final Lock lock = new ReentrantLock();
    private final Condition cond = lock.newCondition();

    private final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor(new ThreadFactory(){
        @Override
        public Thread newThread(Runnable r) {
            Thread thread = new Thread(r, "DynamicSocketPoolLatencyCleaner");
            thread.setDaemon(true);
            thread.setPriority(Thread.MIN_PRIORITY);
            return thread;
        }
    });

    public DynamicSocketPool(SocketPoolConfig config) throws UnknownHostException, SocketPoolTimeOutException {
        super(config.getHost(), config.getPort(), config.getSocketReadTimeout(), config.getWaitingTimeout(),
                config.getReconnectTimeout(), config.getInitializeTimeout(), config.getDisconnectBound(), config.getType());

        if (config.getMinPoolSize() < 0 || config.getMaxPoolSize() > config.getMaxPoolSize() || config.getMaxPoolSize() == 0) {
            throw new IllegalArgumentException("Incorrect value of min or max pool size");
        }

        if (config.getLatencyPeriod() < 0) {
            throw new IllegalArgumentException("Incorrect value of latency time");
        }

        this.minPoolSize = config.getMinPoolSize();
        this.maxPoolSize = config.getMaxPoolSize();
        this.latency = config.getLatencyPeriod();

        initializePool();

        startSocketPoolCleaner();
    }

    private void initializePool() throws SocketPoolTimeOutException {
        long startTime = System.currentTimeMillis();

        for (int i = 0; i < minPoolSize;) {
            lock.lock();
            try {
                if (stateMachine.isClosed()) {
                    LOG.info("Socket pool is closed, initialization aborted");
                    return; // Socket pool is closed
                }

                if (queue.size() + currentUsed > minPoolSize) {
                    break;
                }

                try {
                    SocketWorkerInternal socketWorker = socketWorkerFactory.create();
                    queue.add(socketWorker);
                    i++;
                } catch (IOException e) {
                    LOG.warn("Can't establish socket connection because: Exception - " +
                            e.getClass().getSimpleName() + " and case - " + e.getMessage());

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
                throw new SocketPoolTimeOutException("Initialize pool timeout occur");
            }
        }
    }

    private void startSocketPoolCleaner() {
        Runnable cleanTask = new Runnable() {
            @Override
            public void run() {
                long scheduleTime = latency;

                lock.lock();
                try {
                    if (stateMachine.isClosed()) {
                        LOG.info("Socket pool is closed, cleaner thread stopped");
                        return;
                    }

                    if (queue.size() + currentUsed <= minPoolSize) {
                        return;
                    }

                    long remainingTime, currentTime = System.currentTimeMillis();
                    while(!queue.isEmpty()) {
                        SocketWorkerInternal socketWorker = queue.peek();
                        assert socketWorker != null: "Priority queue must contain socket worker";

                        remainingTime = currentTime - socketWorker.getLastTimeStamp();
                        if (remainingTime > latency) {
                            SocketWorkerInternal removed = queue.remove();
                            assert removed == socketWorker: "Incorrect operation peek and remove";

                            socketWorker.close();
                        } else {
                            scheduleTime = remainingTime;
                            break;
                        }
                    }
                } finally {
                    lock.unlock();

                    if (!stateMachine.isClosed()) {
                        scheduler.schedule(this, MIN_LATENCY_PERIOD + scheduleTime, TimeUnit.MILLISECONDS);
                    }
                }
            }
        };

        if (!stateMachine.isClosed()) {
            scheduler.schedule(cleanTask, MIN_LATENCY_PERIOD + latency, TimeUnit.MILLISECONDS);
        }
    }

    public SocketWorker borrowSocketWorker() throws InterruptedException, SocketPoolException {
        SocketWorkerInternal socketWorker;

        lock.lock();
        try {
            if (stateMachine.isClosed()) {
                throw new SocketPoolClosedException("Socket pool is closed, borrowing of socket worker was rejected");
            }

            if (stateMachine.isReconnecting()) {
                throw new SocketPoolUnavailableException("Socket pool is reconnecting, borrowing of socket worker was rejected");
            }

            do {
                socketWorker = queue.poll();
                if (socketWorker == null) {
                    if (currentUsed < maxPoolSize) {
                        try {
                            socketWorker = socketWorkerFactory.create();
                        } catch (IOException e) {
                            LOG.warn("Can't establish socket connection because: Exception - " +
                                    e.getClass().getSimpleName() + " and case - " + e.getMessage());
                            throw new SocketPoolUnavailableException("Can't create extra socket", e);
                        }
                    } else {
                        while(queue.isEmpty()) {
                            if (!cond.await(waitingTimeout, TimeUnit.MILLISECONDS)) {
                                throw new SocketPoolTimeOutException("Timeout is occurred while wait for socket worker");
                            }
                        }

                        socketWorker = queue.poll();
                        assert socketWorker != null: "Incorrect state of queue";
                    }
                }
            } while(socketWorker == null);

            currentUsed++;

        } finally {
            lock.unlock();
        }

        return socketWorker;
    }

    public void internalReturnSocketWorker(SocketWorkerInternal socketWorker) {
        lock.lock();
        try {
            if (stateMachine.isClosed()) {
                socketWorker.close();
                LOG.info("Socket pool is closed, return operation skipped");
                return;
            }

            if (currentUsed > 0) {
                currentUsed--;
            }

            queue.offer(socketWorker);

            cond.signal();
        } finally {
            lock.unlock();
        }
    }

    @Override
    void feedReconnect() {
        lock.lock();
        try {
            for (SocketWorkerInternal worker: queue) {
                pushToReconnect(worker);
            }
            queue.clear();
        } finally {
            lock.unlock();
        }
    }

    void internalClose() {
        LOG.info("Socket pool internal close ...");

        lock.lock();
        try {
            scheduler.shutdownNow();
            for(SocketWorkerInternal socketWorker: queue) {
                socketWorker.close();
            }
        } finally {
            lock.unlock();
        }
    }
}
