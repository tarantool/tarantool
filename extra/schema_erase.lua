lua _schema = box.space[box.schema.SCHEMA_ID]
lua _space = box.space[box.schema.SPACE_ID]
lua _index = box.space[box.schema.INDEX_ID]
-- destroy everything - save snapshot produces an empty snapshot now
lua _schema:run_triggers(false)
lua _schema:truncate()
lua _space:run_triggers(false)
lua _space:truncate()
lua _index:run_triggers(false)
lua _index:truncate()

