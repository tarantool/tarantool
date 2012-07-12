package tarantool.connector.socketpool.worker;

import java.io.IOException;

public interface SocketFactory {
    public SocketWorkerInternal create() throws IOException;
}
