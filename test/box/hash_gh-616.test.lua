-- 
-- gh-616 "1-based indexing and 0-based error message
--
_ = box.schema.create_space('test')
_ = box.space.test:create_index('i',{parts={1,'string'}})
box.space.test:insert{1}
box.space.test:drop()
