.. confval:: replication_source

    If replication_source is not an empty string, the server is considered
    to be a Tarantool replica. The replica server will try to connect to the
    master which replication_source specifies with a :ref:`URI` (Universal
    Resource Identifier), for example :samp:`{konstantin}:{secret_password}@{tarantool.org}:{3301}`.

    The default user name is ‘guest’.
    A replica server does not accept data-change requests on the :confval:`listen` port.
    The replication_source parameter is
    dynamic, that is, to enter master mode, simply set replication_source
    to an empty string and issue :code:`box.cfg{replication_source=`:samp:`{new-value}`:code:`}`.

    Type: string |br|
    Default: null |br|
    Dynamic: **yes** |br|
