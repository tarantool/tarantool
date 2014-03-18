_schema = box.space[box.schema.SCHEMA_ID]
_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
_user = box.space[box.schema.USER_ID]
_func = box.space[box.schema.FUNC_ID]
_priv = box.space[box.schema.PRIV_ID]
-- destroy everything - save snapshot produces an empty snapshot now
_schema:run_triggers(false)
_schema:truncate()
_space:run_triggers(false)
_space:truncate()
_index:run_triggers(false)
_index:truncate()
_user:run_triggers(false)
_user:truncate()
_func:run_triggers(false)
_func:truncate()
_priv:run_triggers(false)
_priv:truncate()
