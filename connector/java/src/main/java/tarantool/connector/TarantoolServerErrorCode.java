package tarantool.connector;

import java.util.HashMap;
import java.util.Map;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;


public class TarantoolServerErrorCode {
    private static final Log LOG = LogFactory.getLog(TarantoolServerErrorCode.class);

    public final static TarantoolServerErrorCode ERR_CODE_OK;
    public final static TarantoolServerErrorCode ERR_CODE_NONMASTER;
    public final static TarantoolServerErrorCode ERR_CODE_ILLEGAL_PARAMS;
    public final static TarantoolServerErrorCode ERR_CODE_NODE_IS_RO;
    public final static TarantoolServerErrorCode ERR_CODE_NODE_IS_LOCKED;
    public final static TarantoolServerErrorCode ERR_CODE_MEMORY_ISSUE;
    public final static TarantoolServerErrorCode ERR_CODE_UNSUPPORTED_COMMAND;
    public final static TarantoolServerErrorCode ERR_CODE_WRONG_FIELD;
    public final static TarantoolServerErrorCode ERR_CODE_WRONG_NUMBER;
    public final static TarantoolServerErrorCode ERR_CODE_DUPLICATE;
    public final static TarantoolServerErrorCode ERR_CODE_NOTHING;
    public final static TarantoolServerErrorCode ERR_CODE_WRONG_VERSION;
    public final static TarantoolServerErrorCode ERR_CODE_UNKNOWN_ERROR;

    private int codeId;
    private String error;

    private static Map<Integer, TarantoolServerErrorCode> ERROR_CODE_MAPS;

    static {
        ERROR_CODE_MAPS = new HashMap<Integer, TarantoolServerErrorCode>(13);
        ERR_CODE_OK = new TarantoolServerErrorCode(0x00000000, "The query was executed without errors");
        ERR_CODE_NONMASTER = new TarantoolServerErrorCode(0x00000102, "An attempt was made to change data on a read-only port");
        ERR_CODE_ILLEGAL_PARAMS = new TarantoolServerErrorCode(0x00000202, "Incorrectly formatted query");
        ERR_CODE_NODE_IS_RO = new TarantoolServerErrorCode(0x00000401, "The requested data is blocked from modification");
        ERR_CODE_NODE_IS_LOCKED = new TarantoolServerErrorCode(0x00000601, "The requested data is not available");
        ERR_CODE_MEMORY_ISSUE = new TarantoolServerErrorCode(0x00000701, "An error occurred when allocating memory");
        ERR_CODE_UNSUPPORTED_COMMAND = new TarantoolServerErrorCode(0x00000a02, "The query is not recognized");
        ERR_CODE_WRONG_FIELD = new TarantoolServerErrorCode(0x00001E02, "An unknown field was requested");
        ERR_CODE_WRONG_NUMBER = new TarantoolServerErrorCode(0x00001F02, "An out-of-range numeric value was included in the query");
        ERR_CODE_DUPLICATE = new TarantoolServerErrorCode(0x00002002, "An attempt was made to create an object with an existing key");
        ERR_CODE_NOTHING = new TarantoolServerErrorCode(0x00002400, "The query does not support data modification or return");
        ERR_CODE_WRONG_VERSION = new TarantoolServerErrorCode(0x00002602, "The protocol version is not supported");
        ERR_CODE_UNKNOWN_ERROR = new TarantoolServerErrorCode(0x00002702, "Unknown error");
    }

    public static TarantoolServerErrorCode getErrorBy(int errorCode) {
        if (errorCode == ERR_CODE_OK.codeId) {
            return null;
        }
        final TarantoolServerErrorCode tarantoolServerErrorCode = ERROR_CODE_MAPS.get(errorCode);
        if (tarantoolServerErrorCode == null) {
            LOG.warn("Not supported code received from server: " + errorCode);
            return ERR_CODE_UNKNOWN_ERROR;
        }
        return tarantoolServerErrorCode;
    }

    public TarantoolServerErrorCode(int codeId, String error) {
        this.codeId = codeId;
        this.error = error;
        TarantoolServerErrorCode previousError = ERROR_CODE_MAPS.put(codeId, this);
        if (previousError == null) {
            return;
        }
        throw new IllegalStateException("Duplicate error code: " + codeId + " for errors: " + error + ", " + previousError.getError());
    }

    public String getError() {
        return error;
    }

    public int getCodeId() {
        return codeId;
    }

    @Override
    public String toString() {
        return "TarantoolServerErrorCode{" +
                "codeId=" + codeId +
                ", error='" + error + '\'' +
                '}';
    }
}
