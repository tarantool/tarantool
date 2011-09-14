package tarantool.connector.socketpool;


class SocketPoolStateMachine {
    // state  diagram
    enum PoolState {
        RUNNING {
            @Override
            PoolState connect() {
                return RUNNING;
            }
            @Override
            PoolState disconnect() {
                return RECONNECTING;
            }
            @Override
            PoolState close() {
                return CLOSED;
            }},
        RECONNECTING {
            @Override
            PoolState connect() {
                return RUNNING;
            }
            @Override
            PoolState disconnect() {
                return RECONNECTING;
            }
            @Override
            PoolState close() {
                return CLOSED;
            }},
        CLOSED {
            @Override
            PoolState connect() {
                throw new IllegalStateException("Can't change closed pool state");
            }
            @Override
            PoolState disconnect() {
                throw new IllegalStateException("Can't change closed pool state");
            }
            @Override
            PoolState close() {
                throw new IllegalStateException("Can't change closed pool state");
            }};

        abstract PoolState connect();
        abstract PoolState disconnect();
        abstract PoolState close();
    }

    private volatile PoolState state = PoolState.RUNNING;

    public void connect() {
        state = state.connect();
    }

    public void disconnect() {
        state = state.disconnect();
    }

    public void close() {
        state = state.close();
    }

    public boolean isRunning() {
        return state == PoolState.RUNNING;
    }

    public boolean isReconnecting() {
        return state == PoolState.RECONNECTING;
    }

    public boolean isClosed() {
        return state == PoolState.CLOSED; 
    }
}
