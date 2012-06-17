package tarantool.connector;

public class Constans{
    static public final int HEADER_LENGTH = 12;

    static public final int SELECT_REQUEST_BODY = 20; 
    static public final int INSERT_REQUEST_BODY = 8;
    static public final int UPDATE_REQUEST_BODY = 12;
    static public final int CALL_REQUEST_BODY = 4;
    static public final int DELETE_REQUEST_BODY = 8; 
    
    static public final int REQ_TYPE_INSERT = 13;
    static public final int REQ_TYPE_SELECT = 17;
    static public final int REQ_TYPE_UPDATE = 19;
    static public final int REQ_TYPE_DELETE = 21;
    static public final int REQ_TYPE_CALL = 22;
    static public final int REQ_TYPE_PING = 65280;
    
    static public final int OP_ASSIGN = 0;
    static public final int OP_ADD = 1;
    static public final int OP_AND = 2;
    static public final int OP_XOR = 3;
    static public final int OP_OR = 4;
    static public final int OP_SPLICE = 5;
    static public final int OP_DELETE = 6;
    static public final int OP_INSERT = 7;
}
