* Now struct fiber includes a map-type field
  for context that is inherited when a new fiber is created.
  The command get_cnt() is used to retrieve the metadata,
  set_cnt() is used to modify it.
