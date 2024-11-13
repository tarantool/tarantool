local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('leader_reassignment')

g.before_all(function(cg)
    -- Создаем кластер
    cg.cluster = cluster:new{}
    -- Настройка конфигурации box для всех серверов
    local box_cfg = {
        election_mode = 'candidate',
        replication_timeout = 0.1,  -- Увеличено время ожидания репликации
        replication = {
            server.build_listen_uri('node1', cg.cluster.id),
            server.build_listen_uri('node2', cg.cluster.id),
            server.build_listen_uri('node3', cg.cluster.id),
        },
    }

    -- Добавляем сервера в кластер
    cg.node1 = cg.cluster:build_and_add_server({alias = 'node1', box_cfg = box_cfg})
    cg.node2 = cg.cluster:build_and_add_server({alias = 'node2', box_cfg = box_cfg})
    cg.node3 = cg.cluster:build_and_add_server({alias = 'node3', box_cfg = box_cfg})

    -- Запуск кластера
    cg.cluster:start()

    -- Проверка подключения каждого узла
    for _, node in ipairs({cg.node1, cg.node2, cg.node3}) do
        t.helpers.retrying({}, function()
            t.assert_equals(node.net_box.state, 'active', 'Node is connected')
            t.assert_equals(node.net_box:eval('return box.info.status'), 'running', 'Node is running')
        end)
    end
end)

g.after_all(function(cg)
    -- Остановка и удаление кластера после теста
    cg.cluster:drop()
end)

-- Тест назначения нового лидера после завершения работы текущего
g.test_leader_reassignment = function(cg)
    -- Проверяем, что у кластера есть лидер
    local initial_leader = cg.cluster:get_leader()
    t.assert(initial_leader ~= nil, 'Изначальный лидер назначен')

    -- Останавливаем текущего лидера
    initial_leader:stop()

    -- Ожидаем выбора нового лидера
    local new_leader
    t.helpers.retrying({}, function()
        new_leader = cg.cluster:get_leader()
        t.assert(new_leader ~= nil and new_leader ~= initial_leader, 'Назначен новый лидер')
    end)

    -- Проверяем, что новый лидер отличается от предыдущего
    t.assert_not_equals(new_leader, initial_leader, 'Назначен новый лидер после остановки предыдущего')
end

