/* lineitem.h is automatically generated by tbl2c.lua */
#ifdef __cplusplus
extern "C" {
#endif

struct datetime {
    int year, month, day;
};

struct lineitem {
        long l_orderkey;
        long l_partkey;
        long l_suppkey;
        long l_linenumber;
        long l_quantity;
        double l_extendedprice;
        double l_discount;
        double l_tax;
        const char * l_returnflag;
        const char * l_linestatus;
        struct datetime l_shipdate;
        struct datetime l_commitdate;
        struct datetime l_receiptdate;
        const char * l_shipinstruct;
        const char * l_shipmode;
        const char * l_comment;
};

extern const struct lineitem lineitem[];


#ifdef __cplusplus
}
#endif

