package tarantool.common;


public class ByteUtil {
    
    public static final int LENGTH_SHORT = 2;
    public static final int LENGTH_INTEGER = 4;
    public static final int LENGTH_LONG = 8;

    public static int writeInteger(byte[] bytes, int position, int value) {
        bytes[position + 3] = (byte) ((value >>> 24) & 0xFF);
        bytes[position + 2] = (byte) ((value >>> 16) & 0xFF);
        bytes[position + 1] = (byte) ((value >>> 8) & 0xFF);
        bytes[position    ] = (byte) ((value) & 0xFF);
        return LENGTH_INTEGER;
    }

    public static int readInteger(byte[] bytes, int position) {
        return ((bytes[position + 3] & 0xFF) << 24) |
               ((bytes[position + 2] & 0xFF) << 16) |
               ((bytes[position + 1] & 0xFF) << 8)  |
               ((bytes[position] & 0xFF));
    }
    
    public static int writeUnsignedInteger(byte[] bytes, int position, long value) {
        bytes[position + 3] = (byte) ((value >>> 24) & 0xFF);
        bytes[position + 2] = (byte) ((value >>> 16) & 0xFF);
        bytes[position + 1] = (byte) ((value >>> 8) & 0xFF);
        bytes[position    ] = (byte) ((value) & 0xFF);
        return LENGTH_INTEGER;
    }
    
    public static long readUnsignedInteger(byte[] bytes, int position) {
        return  ((long)(bytes[position + 3] & 0xFF) << 24) |
                ((long)(bytes[position + 2] & 0xFF) << 16) |
                ((long)(bytes[position + 1] & 0xFF) << 8)  |
                ((long)(bytes[position] & 0xFF));
    }

    public static int writeLong(byte[] bytes, int position, long value) {
        bytes[position + 7] = (byte) ((value >>> 56) & 0xFF);
        bytes[position + 6] = (byte) ((value >>> 48) & 0xFF);
        bytes[position + 5] = (byte) ((value >>> 40) & 0xFF);
        bytes[position + 4] = (byte) ((value >>> 32) & 0xFF);
        bytes[position + 3] = (byte) ((value >>> 24) & 0xFF);
        bytes[position + 2] = (byte) ((value >>> 16) & 0xFF);
        bytes[position + 1] = (byte) ((value >>> 8) & 0xFF);
        bytes[position    ] = (byte) ((value) & 0xFF);
        return LENGTH_LONG;
    }

    public static long readLong(byte[] bytes, int position) {
        return ((long)(bytes[position + 7] & 0xFF) << 56) |
               ((long)(bytes[position + 6] & 0xFF) << 48) |
               ((long)(bytes[position + 5] & 0xFF) << 40) |
               ((long)(bytes[position + 4] & 0xFF) << 32) |
               ((long)(bytes[position + 3] & 0xFF) << 24) |
               ((long)(bytes[position + 2] & 0xFF) << 16) |
               ((long)(bytes[position + 1] & 0xFF) << 8)  |
               ((long)(bytes[position] & 0xFF));
    }

    public static byte[] toBytes(long value) {
        byte[] bytes = new byte[LENGTH_LONG];
        bytes[7] = (byte) ((value >>> 56) & 0xFF);
        bytes[6] = (byte) ((value >>> 48) & 0xFF);
        bytes[5] = (byte) ((value >>> 40) & 0xFF);
        bytes[4] = (byte) ((value >>> 32) & 0xFF);
        bytes[3] = (byte) ((value >>> 24) & 0xFF);
        bytes[2] = (byte) ((value >>> 16) & 0xFF);
        bytes[1] = (byte) ((value >>>  8) & 0xFF);
        bytes[0] = (byte) ((value) & 0xFF);
        return bytes;
    }
    
    public static byte[] toBytes(int value) {
        byte[] bytes = new byte[LENGTH_LONG];
        bytes[3] = (byte) ((value >>> 24) & 0xFF);
        bytes[2] = (byte) ((value >>> 16) & 0xFF);
        bytes[1] = (byte) ((value >>>  8) & 0xFF);
        bytes[0] = (byte) ((value) & 0xFF);
        return bytes;
    }

    public static long toLong(byte[] bytes) {
        return ((long)(bytes[7] & 0xFF) << 56) |
               ((long)(bytes[6] & 0xFF) << 48) |
               ((long)(bytes[5] & 0xFF) << 40) |
               ((long)(bytes[4] & 0xFF) << 32) |
               ((long)(bytes[3] & 0xFF) << 24) |
               ((long)(bytes[2] & 0xFF) << 16) |
               ((long)(bytes[1] & 0xFF) << 8)  |
               ((long)(bytes[0] & 0xFF));
    }
    
    public static int toInt(byte[] bytes) {
        return ((int)(bytes[3] & 0xFF) << 24) |
               ((int)(bytes[2] & 0xFF) << 16) |
               ((int)(bytes[1] & 0xFF) << 8)  |
               ((int)(bytes[0] & 0xFF));
    }

    public static int writeBytes(byte[] bytes, int position, byte[] value, int start, int length) {
        System.arraycopy(value, start, bytes, position, length);
        return length;
    }

    public static int readBytes(byte[] src, int position, byte[] dest, int destPosition, int length) {
        System.arraycopy(src, position, dest, destPosition, length);
        return length;
    }

    public static byte[] convertLongIdToByteArray(long id) {
        byte[] byteId = new byte[LENGTH_LONG];
        ByteUtil.writeLong(byteId, 0, id);
        return byteId;
    }

    /**
     * Compute the space needed to encode the length in BER code.
     *
     * @param length Length to encode
     * @return the count of bytes needed to encode the value <code>length</code>
     */
    public static int sizeOfInVarInt32(int length) {
        if (length < (1 << 7)) {
            return 1;
        }
        if (length < (1 << 14)) {
            return 2;
        }
        if (length < (1 << 21)) {
            return 3;
        }
        if (length < (1 << 28)) {
            return 4;
        }
        return 5;
    }
    
    public static int decodeLengthInVar32Int(byte[] in, int offset) {
        if ((in[offset] & 0x80) == 0) {
            return (in[offset] & 0x7F);
        }
        if ((in[offset + 1] & 0x80) == 0) {
            return (in[offset] & 0x7F) << 7
                    | (in[offset + 1] & 0x7F);
        }
        if ((in[offset + 2] & 0x80) == 0) {
            return (in[offset] & 0x7F) << 14
                    | (in[offset + 1] & 0x7F) << 7
                    | (in[offset + 2] & 0x7F);
        }
        if ((in[offset + 3] & 0x80) == 0) {
            return (in[offset] & 0x7F) << 21
                    | (in[offset + 1] & 0x7F) << 14
                    | (in[offset + 2] & 0x7F) << 7
                    | (in[offset + 3] & 0x7F);
        }
        if ((in[offset + 4] & 0x80) == 0) {
            return (in[offset] & 0x7F) << 28
                    | (in[offset + 1] & 0x7F) << 21
                    | (in[offset + 2] & 0x7F) << 14
                    | (in[offset + 3] & 0x7F) << 7
                    | (in[offset + 4] & 0x7F);
        }
        return 0;
    }


    /**
     * Encodes the length.
     *
     * @param out    an <code>byte[]</code> to which the length is encoded.
     * @param offset position in out where start to write length
     * @param length the length of the object
     */
    public static int encodeLengthInVar32Int(byte[] out, int offset, int length) {
        int currentOffset = offset;
        if (length >= (1 << 7)) {
            if (length >= (1 << 14)) {
                if (length >= (1 << 21)) {
                    if (length >= (1 << 28)) {
                        out[currentOffset++] = (byte) ((length >> 28) | 0x80);
                    }
                    out[currentOffset++] = (byte) ((length >> 21) | 0x80);
                }
                out[currentOffset++] = (byte) ((length >> 14) | 0x80);
            }
            out[currentOffset++] = (byte) ((length >> 7) | 0x80);
        }
        out[currentOffset] = (byte) ((length) & 0x7F);
        return currentOffset - offset + 1;
    }
}
