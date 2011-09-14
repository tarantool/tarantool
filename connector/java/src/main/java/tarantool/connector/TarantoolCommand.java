package tarantool.connector;

import java.util.HashMap;
import java.util.Map;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

public class TarantoolCommand {
    private static final Log LOG = LogFactory.getLog(TarantoolCommand.class);

    private final static Map<Integer, TarantoolCommand> COMMANDS_MAPS;

    public final static TarantoolCommand PING;
    public final static TarantoolCommand INSERT;
    public final static TarantoolCommand SELECT;
    public final static TarantoolCommand UPDATE;
    public final static TarantoolCommand DELETE;
    public final static TarantoolCommand LUA_CALL; // valid as keyset operation in tarantool 1.3.2

    static {
        COMMANDS_MAPS = new HashMap<Integer, TarantoolCommand>(6);
        PING = new TarantoolCommand(0xFF00, "ping");
        INSERT = new TarantoolCommand(0x0D, "insert");
        SELECT = new TarantoolCommand(0x11, "select");
        UPDATE = new TarantoolCommand(0x13, "update");
        DELETE = new TarantoolCommand(0x14, "delete");
        LUA_CALL = new TarantoolCommand(0x15, "luacall");
    }

    private int id;
    private String name;

    public static TarantoolCommand getCommandBy(int id) {
        final TarantoolCommand tarantoolCommand = COMMANDS_MAPS.get(id);
        if (tarantoolCommand == null) {
            LOG.warn("Not supported code received from server with id: " + id);
            return null;
        }
        return tarantoolCommand;
    }

    TarantoolCommand(int id, String name) {
        this.id = id;
        this.name = name;
        TarantoolCommand tarantoolCommand = COMMANDS_MAPS.put(id, this);
        if (tarantoolCommand == null) {
            return;
        }
        throw new IllegalStateException("Duplicate name with id: " + id + " and name: " + name + ", " + tarantoolCommand.getName());
    }

    public int getId() {
        return id;
    }

    public String getName() {
        return name;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (o == null || getClass() != o.getClass()) {
            return false;
        }

        TarantoolCommand that = (TarantoolCommand) o;

        return id == that.id;
    }

    @Override
    public int hashCode() {
        int result = id;
        result = 31 * result + (name != null ? name.hashCode() : 0);
        return result;
    }

    @Override
    public String toString() {
        return "TarantoolCommand{" +
                "id=" + id +
                ", name='" + name + '\'' +
                '}';
    }
}
