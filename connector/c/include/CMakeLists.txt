
#
# tarantool C client library headers
#

install(DIRECTORY libtnt DESTINATION include)

# libtnt
#
#install(FILES
	#libtnt/tnt.h
	#libtnt/tnt_proto.h
	#libtnt/tnt_mem.h
	#libtnt/tnt_enc.h
	#libtnt/tnt_tuple.h
	#libtnt/tnt_iter.h
	#libtnt/tnt_stream.h
	#libtnt/tnt_buf.h
	#libtnt/tnt_ping.h
	#libtnt/tnt_insert.h
	#libtnt/tnt_update.h
	#libtnt/tnt_delete.h
	#libtnt/tnt_select.h
	#libtnt/tnt_call.h
	#libtnt/tnt_reply.h
	#DESTINATION include/libtnt)

## libtntnet
##
#install(FILES
	#libtnt/tnt_opt.h
	#libtnt/tnt_iob.h
	#libtnt/tnt_io.h
	#libtnt/tnt_net.h
	#DESTINATION include/libtnt)

## libtntsql
##
#install(FILES libtnt/tnt_sql.h DESTINATION include/libtnt)
