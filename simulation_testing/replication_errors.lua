


local function monitor_replication(cg)
    local fiber = require('fiber')

    local monitor_config = {
        leader_absent_time = 10, 
        max_terms_change_by_period = 5,
        terms_change_period = 10,
        check_interval = 2,
    }


    local state = {
        term_changes = {}, 
        last_leader_check = fiber.time(),
        last_leader_term = nil,
    }
    while true do

        local leaders = {}

        local problems = {}

        for _, node in ipairs(cg.nodes) do

            local replication_info,election_info = node:exec(function() return box.info.replication,box.info.election end)

            local now = fiber.time()

            if(election_info.state == 'leader') then
                table.insert(leaders, node.id)
            end

            -----------------------------------------------------------------------------------------------------
            
            for id, replica in pairs(replication_info) do
                if ~replica.upstream or replica.upstream.status ~= 'follow'  then
                    
                    table.insert(problems, 'Not connection to replica:'..tostring(id))
                end
            end
            -----------------------------------------------------------------------------------------------------
            print("[Replication Monitor] Detected "..tostring(#leaders).." Leaders:")
            for _, leader in ipairs(leaders) do
                print("[Replication Monitor] Leaders: "..leader)
            end
            if #leaders == 0 then
                if now - state.last_leader_check > monitor_config.leader_absent_time then
                    table.insert(problems, 'No leader detected for more than ' .. monitor_config.leader_absent_time .. ' seconds')
                end
            else
                state.last_leader_check = now
            end
            
            -----------------------------------------------------------------------------------------------------
            local current_term = box.info.election.term
            if current_term ~= state.last_leader_term then
                state.last_leader_term = current_term
                table.insert(state.term_changes, now)
            end

            for i = #state.term_changes, 1, -1 do
                if now - state.term_changes[i] > monitor_config.terms_change_period then
                    table.remove(state.term_changes, i)
                end
            end

            if #state.term_changes > monitor_config.max_terms_change_by_period then
                table.insert(problems, 'Too many term changes in the last ' .. monitor_config.terms_change_period .. ' seconds')
            end
            -----------------------------------------------------------------------------------------------------
            for id, replica in pairs(replication_info) do
                if replica.upstream then
                    local lag = replica.upstream.lag or 0
                    if lag > 0.5 then 
                        table.insert(problems, 'High lag detected on upstream ' .. id)
                    end
                end
                if replica.downstream then
                    local lag = replica.downstream.lag or 0
                    if lag > 0.5 then 
                        table.insert(problems, 'High lag detected on downstream ' .. id)
                    end
                end
            end

            --------------------------------------------------------------------------------------------------------
            for id, replica in pairs(replication_info) do
                if replica.upstream and replica.upstream.status == 'disconnected' then
                    table.insert(problems, 'Upstream disconnected for node ' .. id)
                end
                if replica.downstream and replica.downstream.status == 'disconnected' then
                    table.insert(problems, 'Downstream disconnected for node ' .. id)
                end
            end

            --------------------------------------------------------------------------------------------------------
            print('[Replication Monitor] Detected '..tostring(#problems)..' Problems:')
            if #problems > 0 then
                for _, problem in ipairs(problems) do
                    print('[Replication Monitor] '.. problem)
                end
            end

            

        end

        fiber.sleep(monitor_config.check_interval)
    end
end



return {
    run_replication_monitor = monitor_replication
}