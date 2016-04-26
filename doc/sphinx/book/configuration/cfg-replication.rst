.. _box-cfg-replication-source:

.. confval:: replication_source

    If replication_source is not an empty string, the server is considered
    to be a Tarantool :ref:`replica <box-replication>`. The replica server will try to connect to the
    master which replication_source specifies with a :ref:`URI` (Universal
    Resource Identifier), for example :samp:`{konstantin}:{secret_password}@{tarantool.org}:{3301}`.

    If there is more than one replication source in a cluster, specify an
    array of URIs, for example |br|
    :codenormal:`box.cfg{replication_source = {`:codeitalic:`uri#1,uri#2`:codenormal:`}}` |br|

    If one of the URIs is "self" -- that is, if one of
    the URIs is for the same server that :codenormal:`box.cfg{}` is being executed on --
    then it is ignored. Thus it is possible to use the same replication_source
    specification on multiple servers.

    The default user name is ‘guest’.
    A replica server does not accept data-change requests on the :confval:`listen` port.
    The replication_source parameter is
    dynamic, that is, to enter master mode, simply set replication_source
    to an empty string and issue :code:`box.cfg{replication_source=`:samp:`{new-value}`:code:`}`.

    Type: string |br|
    Default: null |br|
    Dynamic: **yes** |br|
