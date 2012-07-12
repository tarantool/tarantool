package tarantool.connector.socketpool;

class SocketPoolStateMachine {
    // state diagram
    enum PoolState {
        CLOSED {
            @Override
            PoolState close() {
                throw new IllegalStateException(
                        "Can't change closed pool state");
            }

            @Override
            PoolState connect() {
                throw new IllegalStateException(
                        "Can't change closed pool state");
            }

            @Override
            PoolState disconnect() {
                throw new IllegalStateException(
                        "Can't change closed pool state");
            }
        },
        RECONNECTING {
            @Override
            PoolState close() {
                return CLOSED;
            }

            @Override
            PoolState connect() {
                return RUNNING;
            }

            @Override
            PoolState disconnect() {
                return RECONNECTING;
            }
        },
        RUNNING {
            @Override
            PoolState close() {
                return CLOSED;
            }

            @Override
            PoolState connect() {
                return RUNNING;
            }

            @Override
            PoolState disconnect() {
                return RECONNECTING;
            }
        };

        abstract PoolState close();

        abstract PoolState connect();

        abstract PoolState disconnect();
    }

    private volatile PoolState state = PoolState.RUNNING;

    public void close() {
        state = state.close();
    }

    public void connect() {
        state = state.connect();
    }

    public void disconnect() {
        state = state.disconnect();
    }

    public boolean isClosed() {
        return state == PoolState.CLOSED;
    }

    public boolean isReconnecting() {
        return state == PoolState.RECONNECTING;
    }

    public boolean isRunning() {
        return state == PoolState.RUNNING;
    }
}
