box.cfg{
    replicaset_uuid = 'cbf06940-0790-498b-948d-042b62cf3d29',
    instance_uuid = '3de2e3e1-9ebe-4d0d-abb1-26d301b84633',
    replication = {3301, 3302},
    read_only = true,
    listen = 3302,
}

box.snapshot()
os.exit(0)
