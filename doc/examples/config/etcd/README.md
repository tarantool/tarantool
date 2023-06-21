Put the remote config to etcd:

```sh
etcdctl put /foo/config/all < doc/examples/config/etcd/remote.yaml
```

Read the config:

```sh
etcdctl get /foo/config/ /foo/config0
```

Start tarantool replicaset:

```sh
tarantool --name instance-001 --config doc/examples/config/etcd/local.yaml
tarantool --name instance-002 --config doc/examples/config/etcd/local.yaml
tarantool --name instance-003 --config doc/examples/config/etcd/local.yaml
```
