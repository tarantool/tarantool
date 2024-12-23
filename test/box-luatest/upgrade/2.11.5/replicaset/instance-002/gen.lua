-- Tarantool 2.11.5-entrypoint-103-ga2cc2b0080. Before release of the 2.11.5.
box.cfg{
    replicaset_uuid = 'b5c6e102-aa65-4b5f-a967-ee2a4f5d1480',
    instance_uuid = '30d791cf-3d88-4a62-81d6-9fe5e45dc44c',
    replication = {3301, 3302},
    read_only = true,
    listen = 3302,
}

box.snapshot()
os.exit(0)
