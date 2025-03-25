## bugfix/config

* Now runtime `lua_call` privileges are also applied before the initial
  bootstrap, making it possible to permit some functions to be executed by the
  guest user before setting up the cluster.
