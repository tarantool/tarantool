package tarantool.connector;


import java.io.IOException;

import tarantool.connector.exception.TarantoolConnectorException;
import tarantool.connector.socketpool.worker.SocketWorker;
import tarantool.common.ByteUtil;


public class RequestResponseFormatter {
    
    public static final int HEADER_LENGTH = 12;
    
    private static final int INSERT_PACKET_SIZE = RequestResponseFormatter.HEADER_LENGTH + 20 + ByteUtil.sizeOfInVarInt32(4);
    private static final int GET_PACKET_SIZE = RequestResponseFormatter.HEADER_LENGTH + 32 + ByteUtil.sizeOfInVarInt32(4);
    private static final int DELETE_PACKET_SIZE = RequestResponseFormatter.HEADER_LENGTH + 16 + ByteUtil.sizeOfInVarInt32(4);
    private static final int KEY_SET_PACKET_SIZE = RequestResponseFormatter.HEADER_LENGTH + 4;
    private static final int LUA_SCRIPT_PACKET_SIZE = RequestResponseFormatter.HEADER_LENGTH + 8;

    // ascii string "get_all_pkeys"
    private static final byte[] GET_ALL_KEYS_LUA_SCRIPT = {103, 101, 116, 95, 97, 108, 108, 95, 112, 107, 101, 121, 115};

    public static byte[] createInsertCommand(byte[] uid, int requestId, int nameSpaceId, byte[] data) {
        final byte[] packet = new byte[INSERT_PACKET_SIZE + ByteUtil.sizeOfInVarInt32(data.length) + data.length];

        int offset = createHeader(packet, TarantoolCommand.INSERT.getId(), requestId);

        // 4 bytes - Namespace number
        offset += ByteUtil.writeInteger(packet, offset, nameSpaceId);
        // 4 bytes - <flags, uint32_t>
        offset += ByteUtil.writeInteger(packet, offset, 0);
        // 4 bytes - <cardinality, uint32_t>: Number of fields in the tuple
        offset += ByteUtil.writeInteger(packet, offset, 2);

        /**
         * <field[0], field_t>
         *      field_t: <size, varint32><data, uint8_t[]>, The field with the user data of indicated length
         * ...
         * <field[cardinality-1], field_t>
         */
        // N bytes - field_t: <size, varint32>
        offset += ByteUtil.encodeLengthInVar32Int(packet, offset, 8);
        // 4 bytes - <data, uint8_t[]>
        offset += ByteUtil.writeBytes(packet, offset, uid, 0, uid.length);
        // N bytes - field_t: <size, varint32>
        offset += ByteUtil.encodeLengthInVar32Int(packet, offset, data.length);
        // N bytes - <data, uint8_t[]>
        ByteUtil.writeBytes(packet, offset, data, 0, data.length);

        return packet;
    }

    public static byte[] createGetDataCommand(byte[] uid, int requestId, int nameSpaceId) {
        final byte[] packet = new byte[GET_PACKET_SIZE];

        int offset = createHeader(packet, TarantoolCommand.SELECT.getId(), requestId);

        // 4 bytes - Namespace number
        offset += ByteUtil.writeInteger(packet, offset, nameSpaceId);
        // 4 bytes - Index number
        offset += ByteUtil.writeInteger(packet, offset, 0);
        // 4 bytes - Offset of the return data
        offset += ByteUtil.writeInteger(packet, offset, 0);
        /**
         * 4 bytes - Maximal number of results in the answer
         * -1 UINT32_MAX actually
         */
        offset += ByteUtil.writeInteger(packet, offset, -1);
        // 4 bytes - Number of keys for the current query
        offset += ByteUtil.writeInteger(packet, offset, 1);
        /**
         * <key_cardinality, int32_t><key_fields, tuple_t>
         */
        offset += ByteUtil.writeInteger(packet, offset, 1);
        offset += ByteUtil.encodeLengthInVar32Int(packet, offset, 8);
        ByteUtil.writeBytes(packet, offset, uid, 0, uid.length);

        return packet;
    }

    public static byte[] createDeleteCommand(byte[] uid, int requestId, int nameSpaceId) {
        final byte[] packet = new byte[DELETE_PACKET_SIZE];

        int offset = createHeader(packet, TarantoolCommand.DELETE.getId(), requestId);

        // 4 bytes - Namespace number
        offset += ByteUtil.writeInteger(packet, offset, nameSpaceId);

        /**
         * <key_cardinality, int32_t><key_fields, tuple_t>
         * 8 bytes + VAR32
         */
        offset += ByteUtil.writeInteger(packet, offset, 1);
        offset += ByteUtil.encodeLengthInVar32Int(packet, offset, 8);
        ByteUtil.writeBytes(packet, offset, uid, 0, uid.length);

        return packet;
    }

    public static byte[] createDeliverKeySetCommand(int requestId, int nameSpaceId) {
        byte[] packet = new byte[KEY_SET_PACKET_SIZE];
        int offset = createHeader(packet, TarantoolCommand.LUA_CALL.getId(), requestId);
        ByteUtil.writeInteger(packet, offset, nameSpaceId);
        return packet;
    }

    public static byte[] createScriptDeliverKeySetCommand(int requestId, int nameSpaceId, int batchSize) {
        byte[] batchSizeStr = convertIntToANSI(batchSize);

        int size = GET_ALL_KEYS_LUA_SCRIPT.length + ByteUtil.sizeOfInVarInt32(GET_ALL_KEYS_LUA_SCRIPT.length)
              + batchSizeStr.length + ByteUtil.sizeOfInVarInt32(batchSizeStr.length);

        byte[] packet = new byte[LUA_SCRIPT_PACKET_SIZE + size];
        int offset = createHeader(packet, TarantoolCommand.LUA_CALL.getId(), requestId);
        // 4 bytes - Namespace number
        offset += ByteUtil.writeInteger(packet, offset, nameSpaceId);

        // N bytes - field_t: <size, varint32>
        offset += ByteUtil.encodeLengthInVar32Int(packet, offset, GET_ALL_KEYS_LUA_SCRIPT.length);
        // 4 bytes - <data, uint8_t[]>
        offset += ByteUtil.writeBytes(packet, offset, GET_ALL_KEYS_LUA_SCRIPT, 0, GET_ALL_KEYS_LUA_SCRIPT.length);
        // 4 bytes - arguments count
        offset += ByteUtil.writeInteger(packet, offset, 1);
        /**
         * <key_fields, tuple_t>
         */
        offset += ByteUtil.encodeLengthInVar32Int(packet, offset, batchSizeStr.length);
        ByteUtil.writeBytes(packet, offset, batchSizeStr, 0, batchSizeStr.length);

        return packet;
    }

    public static byte[] createPingCommand(int requestId) {
        byte[] packet = new byte[RequestResponseFormatter.HEADER_LENGTH];
        createHeader(packet, TarantoolCommand.PING.getId(), requestId);
        return packet;
    }

    public static TarantoolResponse responseParser(byte[] header) {
        int dataLength = ByteUtil.readInteger(header, 4);
        int requestId = ByteUtil.readInteger(header, 8);
        return new TarantoolResponse(dataLength, requestId);
    }

    public static int createHeader(byte[] buffer, int command, int requestId) {
        int offset = 0;
        // 4 bytes - The code of the operation which the server must execute
        offset += ByteUtil.writeInteger(buffer, offset, command);
        // 4 bytes - The length of the data packet which does not count the header size
        offset += ByteUtil.writeInteger(buffer, offset, buffer.length - RequestResponseFormatter.HEADER_LENGTH);
        // 4 bytes - A random number which identifies the query
        offset += ByteUtil.writeInteger(buffer, offset, requestId);

        return offset;
    }

    public static byte[] readResponseData(int requestId, SocketWorker worker) throws IOException, TarantoolConnectorException {
        byte[] header = new byte[RequestResponseFormatter.HEADER_LENGTH];
        int readData = worker.readData(header, RequestResponseFormatter.HEADER_LENGTH);
        TarantoolResponse response = responseParser(header);
    
        if (readData != RequestResponseFormatter.HEADER_LENGTH) {
            throw new TarantoolConnectorException("Incorrect size of response header");
        }
    
        if (requestId != response.getRequestId()) {
            throw new TarantoolConnectorException("Incorrect request Id");
        }
    
        int dataLength = response.getDataLength();
        if (dataLength <= 0) {
            throw new TarantoolConnectorException("Incorrect size of response result");
        }
    
        byte[] result = new byte[dataLength];
        readData = worker.readData(result, dataLength);
    
        if (readData != dataLength) {
            throw new TarantoolConnectorException("Wrong size of data or length of data that was read");
        }
    
        return result;
    }

    private static byte[] convertIntToANSI(int value) {
		int count = 0;
		int index = value < 0? 1: 0;
		int abs = Math.abs(value);
		long delimiter = 1;
		for (;;) {
			count++;
			if (abs / (delimiter * 10) == 0) {
				break;
			}
			delimiter *= 10;
		}
		byte[] result = new byte[count + index];
		if (index > 0) {
			result[index - 1] = 45;
		}
		for (; count--> 0; delimiter /= 10) {
			long d = abs / delimiter;
			result[index++] = (byte)(d + 48);
			abs -= d * delimiter;
		}

		return result;
	}
}
