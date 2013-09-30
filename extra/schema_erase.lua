_schema = box.space[box.schema.SCHEMA_ID]
_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
-- destroy everything - save snapshot produces an empty snapshot now
_schema:run_triggers(false)
_schema:truncate()
_space:run_triggers(false)
_space:truncate()
_index:run_triggers(false)
_index:truncate()

