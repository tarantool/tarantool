package tarantool.connector;

import java.io.IOException;
import java.net.UnknownHostException;
import java.util.concurrent.atomic.AtomicInteger;

import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.exception.TarantoolUnavailableException;
import tarantool.connector.socketpool.SocketPool;
import tarantool.connector.socketpool.SocketPoolFactory;
import tarantool.connector.socketpool.exception.SocketPoolException;
import tarantool.connector.socketpool.exception.SocketPoolTimeOutException;
import tarantool.connector.socketpool.worker.SocketWorker;
import tarantool.common.ByteUtil;


public class TarantoolConnectorImpl implements TarantoolConnector {

    private final int nameSpace;
    private final AtomicInteger requestId = new AtomicInteger(1);

    private final SocketPool pool;

    private interface InternalParser<T> {
        void allocateMemory(int size);
        void parse(byte[] buffer, int offset, int index);
        T getResult();
        T getEmptyResult();
    }

    private static class LongParser implements InternalParser<long[]> {
        long[] keys;

        @Override
        public void allocateMemory(int size) {
            keys = new long[size];
        }

        @Override
        public void parse(byte[] buffer, int offset, int index) {
            keys[index] = ByteUtil.readLong(buffer, offset);
        }

        @Override
        public long[] getResult() {
            return keys;
        }

        @Override
        public long[] getEmptyResult() {
            return new long[0];
        }
    }

    private static class ByteArrayParser implements InternalParser<byte[][]> {
        private byte[][] keys;

        @Override
        public void allocateMemory(int size) {
            keys = new byte[size][ByteUtil.LENGTH_LONG];
        }

        @Override
        public void parse(byte[] buffer, int offset, int index) {
            ByteUtil.readBytes(buffer, offset, keys[index], 0, ByteUtil.LENGTH_LONG);
        }

        @Override
        public byte[][] getResult() {
            return keys;
        }

        @Override
        public byte[][] getEmptyResult() {
            return new byte[0][0];
        }
    }

    public TarantoolConnectorImpl(TarantoolConnectorConfig tarantoolConfig)
            throws UnknownHostException, SocketPoolTimeOutException {

        this.nameSpace = tarantoolConfig.getNameSpace();
        this.pool = SocketPoolFactory.createSocketPool(tarantoolConfig.getSocketPoolConfig());
    }

    private int getRequestId() {
        if (requestId.get() == Integer.MAX_VALUE) {
            requestId.compareAndSet(Integer.MAX_VALUE, 1);
        }
        return requestId.getAndIncrement();
    }

    @Override
    public void insertData(long id, byte[] data) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            insert(id, getRequestId(), data, worker);
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public void insertData(byte[] id, byte[] data) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            insert(id, getRequestId(), data, worker);
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public byte[] getData(long id) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            return get(id, getRequestId(), worker);
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public byte[] getData(byte[] id) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            return get(id, getRequestId(), worker);
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public boolean deleteById(long id) throws InterruptedException, TarantoolConnectorException {
        return deleteById(ByteUtil.convertLongIdToByteArray(id));
    }

    @Override
    public boolean deleteById(byte[] id) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            int requestId = getRequestId();
            byte[] packet = RequestResponseFormatter.createDeleteCommand(id, requestId, nameSpace);

            worker.writeData(packet);

            byte[] result = RequestResponseFormatter.readResponseData(requestId, worker);

            final TarantoolServerErrorCode errorCode = TarantoolServerErrorCode.getErrorBy(ByteUtil.readInteger(result, 0));
            if (errorCode == null) {
                int deletedElements = ByteUtil.readInteger(result, 4);
                return deletedElements > 0;
            } else {
                throw new TarantoolConnectorException("Can't delete data by id: " + errorCode);
            }
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public byte[][] getKeySetAsByteArray() throws TarantoolConnectorException, InterruptedException {
        return getKeySet(new ByteArrayParser());
    }

    @Override
    public long[] getKeySetAsLongArray() throws TarantoolConnectorException, InterruptedException {
        return getKeySet(new LongParser());
    }

    @SuppressWarnings("unchecked")
    private <T> T getKeySet(InternalParser parser) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            int requestId = getRequestId();
            byte[] packet = RequestResponseFormatter.createDeliverKeySetCommand(requestId, nameSpace);
            worker.writeData(packet);

            byte[] result = RequestResponseFormatter.readResponseData(requestId, worker);
            final TarantoolServerErrorCode errorCode = TarantoolServerErrorCode.getErrorBy(ByteUtil.readInteger(result, 0));
            if (errorCode == null) {
                int updatedElements = ByteUtil.readInteger(result, 4);
                if (updatedElements > 0) {
                    int offset = 8;
                    parser.allocateMemory(updatedElements);
                    for (int i = 0; i < updatedElements; i++) {
                        offset += 8;
                        int valueLength = ByteUtil.decodeLengthInVar32Int(result, offset);
                        if (valueLength != 8) {
                            throw new TarantoolConnectorException("Get key set operation return incorrect id length: " + valueLength);
                        }

                        offset += ByteUtil.sizeOfInVarInt32(valueLength);
                        parser.parse(result, offset, i);
                        offset += 8;
                    }
                    return (T)parser.getResult();
                } else {
                    return (T)parser.getEmptyResult();
                }
            } else {
                throw new TarantoolConnectorException("Can't get key set: " + errorCode);
            }
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public long[] getScriptKeySetAsLongArray(int batchSize) throws TarantoolConnectorException, InterruptedException {
        return getScriptKeySet(batchSize, new LongParser());
    }

    @Override
    public byte[][] getScriptKeySetAsByteArray(int batchSize) throws TarantoolConnectorException, InterruptedException {
        return getScriptKeySet(batchSize, new ByteArrayParser());
    }

    @SuppressWarnings("unchecked")
    private <T> T getScriptKeySet(int batchSize, InternalParser parser) throws InterruptedException, TarantoolConnectorException {
        SocketWorker worker = getSocketWorker();
        try {
            int requestId = getRequestId();
            byte[] packet = RequestResponseFormatter.createScriptDeliverKeySetCommand(requestId, nameSpace, batchSize);
            worker.writeData(packet);

            byte[] result = RequestResponseFormatter.readResponseData(requestId, worker);
            final TarantoolServerErrorCode errorCode = TarantoolServerErrorCode.getErrorBy(ByteUtil.readInteger(result, 0));
            if (errorCode == null) {
                int updatedElements = ByteUtil.readInteger(result, 4);
                if (updatedElements > 0) {
                    int offset = 8;
                    parser.allocateMemory(updatedElements);
                    for (int i = 0; i < updatedElements; i++) {
                        int valueLength = ByteUtil.decodeLengthInVar32Int(result, offset);
                        if (valueLength != 8) {
                            throw new TarantoolConnectorException("Get key set operation return incorrect id length: " + valueLength);
                        }

                        offset += ByteUtil.sizeOfInVarInt32(valueLength);
                        parser.parse(result, offset, i);
                        offset += 8;
                    }
                    return (T)parser.getResult();
                } else {
                    return (T)parser.getEmptyResult();
                }
            } else {
                throw new TarantoolConnectorException("Can't get key set: " + errorCode);
            }
        } catch (IOException e) {
            throw new TarantoolConnectorException("Can't correct make socket operation", e);
        } finally {
            worker.release();
        }
    }

    @Override
    public void truncate() throws InterruptedException, TarantoolConnectorException {
        long[] keySet = getKeySetAsLongArray();
        for(long key: keySet) {
            deleteById(key);
        }
    }

    @Override
    public void close() {
        pool.close();
    }

    private SocketWorker getSocketWorker() throws InterruptedException, TarantoolConnectorException {
        try {
            return pool.borrowSocketWorker();
        } catch (SocketPoolTimeOutException e) {
            throw new TarantoolConnectorException("There are no free sockets", e);
        } catch (SocketPoolException e) {
            throw new TarantoolUnavailableException("Socket pool unavailable", e);
        }
    }

    private void insert(long id, int requestId, byte[] data, SocketWorker worker)
            throws IOException, TarantoolConnectorException {
        insert(ByteUtil.convertLongIdToByteArray(id), requestId, data, worker);
    }

    private void insert(byte[] id, int requestId, byte[] data, SocketWorker worker)
            throws IOException, TarantoolConnectorException {
        byte[] packet = RequestResponseFormatter.createInsertCommand(id, requestId, nameSpace, data);
        worker.writeData(packet);

        byte[] result = RequestResponseFormatter.readResponseData(requestId, worker);

        final TarantoolServerErrorCode errorCode = TarantoolServerErrorCode.getErrorBy(ByteUtil.readInteger(result, 0));
        if (errorCode == null) {
            int updatedElements = ByteUtil.readInteger(result, 4);
            if (updatedElements != 1) {
                throw new TarantoolConnectorException("Incorrect number [" + updatedElements + "] of updated elements during insert operation.");
            }
        } else {
            throw new TarantoolConnectorException("Can't insert data: " + errorCode);
        }
    }

    private byte[] get(long id, int requestId, SocketWorker worker)
            throws IOException, TarantoolConnectorException {
        return get(ByteUtil.convertLongIdToByteArray(id), requestId, worker);
    }

    private byte[] get(byte[] id, int requestId, SocketWorker worker)
            throws IOException, TarantoolConnectorException {
        byte[] packet = RequestResponseFormatter.createGetDataCommand(id, requestId, nameSpace);
        worker.writeData(packet);

        byte[] result = RequestResponseFormatter.readResponseData(requestId, worker);
        final TarantoolServerErrorCode errorCode = TarantoolServerErrorCode.getErrorBy(ByteUtil.readInteger(result, 0));
        if (errorCode == null) {
            int updatedElements = ByteUtil.readInteger(result, 4);
            if (updatedElements == 1) {
                // skip lenght of the data (4 bytes), number of fields (4 bytes), uid (5 bytes)
                int valueLength = ByteUtil.decodeLengthInVar32Int(result, 25);
                int offset = ByteUtil.sizeOfInVarInt32(valueLength);
                final byte[] values = new byte[valueLength];
                ByteUtil.readBytes(result, 25 + offset, values, 0, valueLength);
                return values;
            } else {
                return new byte[0];
            }
        } else {
            throw new TarantoolConnectorException("Can't get data: " + errorCode);
        }
    }
}
