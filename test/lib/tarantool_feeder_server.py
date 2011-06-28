import ConfigParser
from tarantool_server import TarantoolServer, TarantoolConfigFile

class TarantoolFeederServer(TarantoolServer):
    def __new__(cls, core="tarantool", module="feeder"):
        return TarantoolServer.__new__(cls)

    def __init__(self, core="tarantool", module="feeder"):
        TarantoolServer.__init__(self, core, module)

    def configure(self, config):
        TarantoolServer.configure(self, config)
        with open(self.config) as fp:
            dummy_section_name = "tarantool"
            config = ConfigParser.ConfigParser()
            config.readfp(TarantoolConfigFile(fp, dummy_section_name))
            self.port = int(config.get(dummy_section_name, "wal_feeder_bind_port"))

