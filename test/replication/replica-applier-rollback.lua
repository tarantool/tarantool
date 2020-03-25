--
-- vim: ts=4 sw=4 et
--

print('arg', arg)

box.cfg({
    replication                 = os.getenv("MASTER"),
    listen                      = os.getenv("LISTEN"),
    memtx_memory                = 107374182,
    replication_timeout         = 0.1,
    replication_connect_timeout = 0.5,
    read_only                   = true,
})

require('console').listen(os.getenv('ADMIN'))
