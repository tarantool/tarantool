return {
  -- XXX: Max nins is limited by max IRRef, that equals to
  -- REF_DROP - REF_BIAS. Unfortunately, these constants are not
  -- provided to Lua space, so we ought to make some math:
  -- * REF_DROP = 0xffff
  -- * REF_BIAS = 0x8000
  maxnins = 0xffff - 0x8000,
}
