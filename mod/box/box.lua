--
--
--
function box.select(namespace, index, ...)
    key = {...}
    return select(2, -- skip the first return from select, number of tuples
        box.process(17, box.pack('iiiiii'..string.rep('p', #key),
                                 namespace,
                                 index,
                                 0, -- offset
                                 4294967295, -- limit
                                 1, -- key count
                                 #key, -- key cardinality
                                 unpack(key))))
end
--
-- delete can be done only by the primary key, whose
-- index is always 0. It doesn't accept compound keys
--
function box.delete(namespace, key)
    return select(2, -- skip the first return, tuple count
        box.process(21, box.pack('iiip', namespace,
                                 1, -- flags, BOX_RETURN_TUPLE
                                 1, -- cardinality
                                 key)))
end

-- insert or replace a tuple
function box.replace(namespace, ...)
    tuple = {...}
    return select(2,
        box.process(13, box.pack('iii'..string.rep('p', #tuple),
                                 namespace,
                                 1, -- flags, BOX_RETURN_TUPLE 
                                 #tuple, -- cardinality
                                 unpack(tuple))))
end

box.insert = box.replace

function box.update(namespace, key, format, ...)
    ops = {...}
    return select(2,
        box.process(19, box.pack('iiipi'..format,
                                  namespace,
                                  1, -- flags, BOX_RETURN_TUPLE
                                  1, -- cardinality
                                  key, -- primary key
                                  #ops/2, -- op count
                                  ...)))
end
