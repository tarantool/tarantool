import time
import shutil
import subprocess
import yaml
import ConfigParser
from tarantool_server import TarantoolServer, TarantoolConfigFile
from admin_connection import AdminConnection
from box_connection import BoxConnection
from memcached_connection import MemcachedConnection
import time

class TarantoolBoxServer(TarantoolServer):
    def __new__(cls, core="tarantool", module="box"):
        return TarantoolServer.__new__(cls)

    def __init__(self, core="tarantool", module="box"):
        TarantoolServer.__init__(self, core, module)

    def configure(self, config):
        TarantoolServer.configure(self, config)
        with open(self.config) as fp:
            dummy_section_name = "tarantool"
            config = ConfigParser.ConfigParser()
            config.readfp(TarantoolConfigFile(fp, dummy_section_name))

            self.primary_port = self.get_option_int(config, dummy_section_name, "primary_port")
            self.admin_port = self.get_option_int(config, dummy_section_name, "admin_port")
            self.memcached_port = self.get_option_int(config, dummy_section_name, "memcached_port")

        self.port = self.admin_port
        self.admin = AdminConnection("localhost", self.admin_port)
        self.sql = BoxConnection("localhost", self.primary_port)
        if self.memcached_port != 0:
            # Run memcached client
            self.memcached = MemcachedConnection('localhost', self.memcached_port)

    def get_option_int(self, config, section, option):
        if config.has_option(section, option):
            return config.getint(section, option)
        else:
            return 0

    def init(self):
        # init storage
        subprocess.check_call([self.binary, "--init-storage"],
                              cwd = self.vardir,
                              # catch stdout/stderr to not clutter output
                              stdout = subprocess.PIPE,
                              stderr = subprocess.PIPE)

    def get_param(self, param):
        data = self.admin.execute("show info\n", silent = True)
        info = yaml.load(data)["info"]
        return info[param]

    def wait_lsn(self, lsn):
        while True:
            curr_lsn = int(self.get_param("lsn"))
            if (curr_lsn >= lsn):
                break
            time.sleep(0.01)

