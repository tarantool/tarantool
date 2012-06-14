package tarantool.connector;

public class Constans{
    static public final int HEADER_LENGTH = 12;

    static public final int SELECT_REQUEST_BODY = 20; 
    static public final int INSERT_REQUEST_BODY = 8;
    //static public final int UPDATE_REQUEST_BODY = 12;
    static public final int CALL_REQUEST_BODY = 4;
    static public final int DELETE_REQUEST_BODY = 8; 
    
    static public final int REQ_TYPE_INSERT = 13;
    static public final int REQ_TYPE_SELECT = 17;
    //static public final int REQ_TYPE_UPDATE = 19;
    static public final int REQ_TYPE_DELETE = 21;
    static public final int REQ_TYPE_CALL = 22;
    static public final int REQ_TYPE_PING = 65280;
}
