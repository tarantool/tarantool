#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(192)

local prefix = "collation-"

test:do_test(
    prefix.."0.1",
    function ()
        box.internal.collation.create('unicode_numeric', 'ICU', 'ru-RU', {numeric_collation="ON"})
        box.internal.collation.create('unicode_numeric_s2', 'ICU', 'ru-RU', {numeric_collation="ON", strength="secondary"})
        box.internal.collation.create('unicode_tur_s2', 'ICU', 'tu', {strength="secondary"})
    end
)

test:do_execsql_test(
    prefix.."0.2",
    "pragma collation_list",
    {
        0,"none",
        1,"unicode",
        2,"unicode_ci",
        3,"binary",
        4,"unicode_af_s1",
        5,"unicode_af_s2",
        6,"unicode_af_s3",
        7,"unicode_am_s1",
        8,"unicode_am_s2",
        9,"unicode_am_s3",
        10,"unicode_ar_s1",
        11,"unicode_ar_s2",
        12,"unicode_ar_s3",
        13,"unicode_as_s1",
        14,"unicode_as_s2",
        15,"unicode_as_s3",
        16,"unicode_az_s1",
        17,"unicode_az_s2",
        18,"unicode_az_s3",
        19,"unicode_be_s1",
        20,"unicode_be_s2",
        21,"unicode_be_s3",
        22,"unicode_bn_s1",
        23,"unicode_bn_s2",
        24,"unicode_bn_s3",
        25,"unicode_bs_s1",
        26,"unicode_bs_s2",
        27,"unicode_bs_s3",
        28,"unicode_bs_Cyrl_s1",
        29,"unicode_bs_Cyrl_s2",
        30,"unicode_bs_Cyrl_s3",
        31,"unicode_ca_s1",
        32,"unicode_ca_s2",
        33,"unicode_ca_s3",
        34,"unicode_cs_s1",
        35,"unicode_cs_s2",
        36,"unicode_cs_s3",
        37,"unicode_cy_s1",
        38,"unicode_cy_s2",
        39,"unicode_cy_s3",
        40,"unicode_da_s1",
        41,"unicode_da_s2",
        42,"unicode_da_s3",
        43,"unicode_de__phonebook_s1",
        44,"unicode_de__phonebook_s2",
        45,"unicode_de__phonebook_s3",
        46,"unicode_de_AT_phonebook_s1",
        47,"unicode_de_AT_phonebook_s2",
        48,"unicode_de_AT_phonebook_s3",
        49,"unicode_dsb_s1",
        50,"unicode_dsb_s2",
        51,"unicode_dsb_s3",
        52,"unicode_ee_s1",
        53,"unicode_ee_s2",
        54,"unicode_ee_s3",
        55,"unicode_eo_s1",
        56,"unicode_eo_s2",
        57,"unicode_eo_s3",
        58,"unicode_es_s1",
        59,"unicode_es_s2",
        60,"unicode_es_s3",
        61,"unicode_es__traditional_s1",
        62,"unicode_es__traditional_s2",
        63,"unicode_es__traditional_s3",
        64,"unicode_et_s1",
        65,"unicode_et_s2",
        66,"unicode_et_s3",
        67,"unicode_fa_s1",
        68,"unicode_fa_s2",
        69,"unicode_fa_s3",
        70,"unicode_fi_s1",
        71,"unicode_fi_s2",
        72,"unicode_fi_s3",
        73,"unicode_fi__phonebook_s1",
        74,"unicode_fi__phonebook_s2",
        75,"unicode_fi__phonebook_s3",
        76,"unicode_fil_s1",
        77,"unicode_fil_s2",
        78,"unicode_fil_s3",
        79,"unicode_fo_s1",
        80,"unicode_fo_s2",
        81,"unicode_fo_s3",
        82,"unicode_fr_CA_s1",
        83,"unicode_fr_CA_s2",
        84,"unicode_fr_CA_s3",
        85,"unicode_gu_s1",
        86,"unicode_gu_s2",
        87,"unicode_gu_s3",
        88,"unicode_ha_s1",
        89,"unicode_ha_s2",
        90,"unicode_ha_s3",
        91,"unicode_haw_s1",
        92,"unicode_haw_s2",
        93,"unicode_haw_s3",
        94,"unicode_he_s1",
        95,"unicode_he_s2",
        96,"unicode_he_s3",
        97,"unicode_hi_s1",
        98,"unicode_hi_s2",
        99,"unicode_hi_s3",
        100,"unicode_hr_s1",
        101,"unicode_hr_s2",
        102,"unicode_hr_s3",
        103,"unicode_hu_s1",
        104,"unicode_hu_s2",
        105,"unicode_hu_s3",
        106,"unicode_hy_s1",
        107,"unicode_hy_s2",
        108,"unicode_hy_s3",
        109,"unicode_ig_s1",
        110,"unicode_ig_s2",
        111,"unicode_ig_s3",
        112,"unicode_is_s1",
        113,"unicode_is_s2",
        114,"unicode_is_s3",
        115,"unicode_ja_s1",
        116,"unicode_ja_s2",
        117,"unicode_ja_s3",
        118,"unicode_kk_s1",
        119,"unicode_kk_s2",
        120,"unicode_kk_s3",
        121,"unicode_kl_s1",
        122,"unicode_kl_s2",
        123,"unicode_kl_s3",
        124,"unicode_kn_s1",
        125,"unicode_kn_s2",
        126,"unicode_kn_s3",
        127,"unicode_ko_s1",
        128,"unicode_ko_s2",
        129,"unicode_ko_s3",
        130,"unicode_kok_s1",
        131,"unicode_kok_s2",
        132,"unicode_kok_s3",
        133,"unicode_ky_s1",
        134,"unicode_ky_s2",
        135,"unicode_ky_s3",
        136,"unicode_lkt_s1",
        137,"unicode_lkt_s2",
        138,"unicode_lkt_s3",
        139,"unicode_ln_s1",
        140,"unicode_ln_s2",
        141,"unicode_ln_s3",
        142,"unicode_lt_s1",
        143,"unicode_lt_s2",
        144,"unicode_lt_s3",
        145,"unicode_lv_s1",
        146,"unicode_lv_s2",
        147,"unicode_lv_s3",
        148,"unicode_mk_s1",
        149,"unicode_mk_s2",
        150,"unicode_mk_s3",
        151,"unicode_ml_s1",
        152,"unicode_ml_s2",
        153,"unicode_ml_s3",
        154,"unicode_mr_s1",
        155,"unicode_mr_s2",
        156,"unicode_mr_s3",
        157,"unicode_mt_s1",
        158,"unicode_mt_s2",
        159,"unicode_mt_s3",
        160,"unicode_nb_s1",
        161,"unicode_nb_s2",
        162,"unicode_nb_s3",
        163,"unicode_nn_s1",
        164,"unicode_nn_s2",
        165,"unicode_nn_s3",
        166,"unicode_nso_s1",
        167,"unicode_nso_s2",
        168,"unicode_nso_s3",
        169,"unicode_om_s1",
        170,"unicode_om_s2",
        171,"unicode_om_s3",
        172,"unicode_or_s1",
        173,"unicode_or_s2",
        174,"unicode_or_s3",
        175,"unicode_pa_s1",
        176,"unicode_pa_s2",
        177,"unicode_pa_s3",
        178,"unicode_pl_s1",
        179,"unicode_pl_s2",
        180,"unicode_pl_s3",
        181,"unicode_ro_s1",
        182,"unicode_ro_s2",
        183,"unicode_ro_s3",
        184,"unicode_sa_s1",
        185,"unicode_sa_s2",
        186,"unicode_sa_s3",
        187,"unicode_se_s1",
        188,"unicode_se_s2",
        189,"unicode_se_s3",
        190,"unicode_si_s1",
        191,"unicode_si_s2",
        192,"unicode_si_s3",
        193,"unicode_si__dictionary_s1",
        194,"unicode_si__dictionary_s2",
        195,"unicode_si__dictionary_s3",
        196,"unicode_sk_s1",
        197,"unicode_sk_s2",
        198,"unicode_sk_s3",
        199,"unicode_sl_s1",
        200,"unicode_sl_s2",
        201,"unicode_sl_s3",
        202,"unicode_sq_s1",
        203,"unicode_sq_s2",
        204,"unicode_sq_s3",
        205,"unicode_sr_s1",
        206,"unicode_sr_s2",
        207,"unicode_sr_s3",
        208,"unicode_sr_Latn_s1",
        209,"unicode_sr_Latn_s2",
        210,"unicode_sr_Latn_s3",
        211,"unicode_sv_s1",
        212,"unicode_sv_s2",
        213,"unicode_sv_s3",
        214,"unicode_sv__reformed_s1",
        215,"unicode_sv__reformed_s2",
        216,"unicode_sv__reformed_s3",
        217,"unicode_ta_s1",
        218,"unicode_ta_s2",
        219,"unicode_ta_s3",
        220,"unicode_te_s1",
        221,"unicode_te_s2",
        222,"unicode_te_s3",
        223,"unicode_th_s1",
        224,"unicode_th_s2",
        225,"unicode_th_s3",
        226,"unicode_tn_s1",
        227,"unicode_tn_s2",
        228,"unicode_tn_s3",
        229,"unicode_to_s1",
        230,"unicode_to_s2",
        231,"unicode_to_s3",
        232,"unicode_tr_s1",
        233,"unicode_tr_s2",
        234,"unicode_tr_s3",
        235,"unicode_ug_Cyrl_s1",
        236,"unicode_ug_Cyrl_s2",
        237,"unicode_ug_Cyrl_s3",
        238,"unicode_uk_s1",
        239,"unicode_uk_s2",
        240,"unicode_uk_s3",
        241,"unicode_ur_s1",
        242,"unicode_ur_s2",
        243,"unicode_ur_s3",
        244,"unicode_vi_s1",
        245,"unicode_vi_s2",
        246,"unicode_vi_s3",
        247,"unicode_vo_s1",
        248,"unicode_vo_s2",
        249,"unicode_vo_s3",
        250,"unicode_wae_s1",
        251,"unicode_wae_s2",
        252,"unicode_wae_s3",
        253,"unicode_wo_s1",
        254,"unicode_wo_s2",
        255,"unicode_wo_s3",
        256,"unicode_yo_s1",
        257,"unicode_yo_s2",
        258,"unicode_yo_s3",
        259,"unicode_zh_s1",
        260,"unicode_zh_s2",
        261,"unicode_zh_s3",
        262,"unicode_zh__big5han_s1",
        263,"unicode_zh__big5han_s2",
        264,"unicode_zh__big5han_s3",
        265,"unicode_zh__gb2312han_s1",
        266,"unicode_zh__gb2312han_s2",
        267,"unicode_zh__gb2312han_s3",
        268,"unicode_zh__pinyin_s1",
        269,"unicode_zh__pinyin_s2",
        270,"unicode_zh__pinyin_s3",
        271,"unicode_zh__stroke_s1",
        272,"unicode_zh__stroke_s2",
        273,"unicode_zh__stroke_s3",
        274,"unicode_zh__zhuyin_s1",
        275,"unicode_zh__zhuyin_s2",
        276,"unicode_zh__zhuyin_s3",
        277,"unicode_numeric",
        278,"unicode_numeric_s2",
        279,"unicode_tur_s2"
    }
)

-- we suppose that tables are immutable
local function merge_tables(...)
    local r = {}
    local N = select('#', ...)
    for i = 1, N do
        local tbl = select(i, ...)
        for _, row in ipairs(tbl) do
            table.insert(r, row)
        end
    end
    return r
end

local function insert_into_table(tbl_name, data)
    local sql = string.format([[ INSERT INTO %s VALUES ]], tbl_name)
    local values = {}
    for _, row in ipairs(data) do
        local items = {}
        for _, item in ipairs(row) do
            if type(item) == "string" then
                table.insert(items, "'"..item.."'")
            end
            if type(item) == "number" then
                table.insert(items, item)
            end
        end
        local value = "("..table.concat(items, ",")..")"
        table.insert(values, value)
    end
    values = table.concat(values, ",")
    sql = sql..values
    test:execsql(sql)
end

local data_eng = {
    {1, "Aa"},
    {2, "a"},
    {3, "aa"},
    {4, "ab"},
    {5, "Ac"},
    {6, "Ad"},
    {7, "AD"},
    {8, "aE"},
    {9, "ae"},
    {10, "aba"},
}
local data_num = {
    {21, "1"},
    {22, "2"},
    {23, "21"},
    {24, "23"},
    {25, "3"},
    {26, "9"},
    {27, "0"},
}
local data_symbols = {
    {41, " "},
    {42, "!"},
    {43, ")"},
    {44, "/"},
    {45, ":"},
    {46, "<"},
    {47, "@"},
    {48, "["},
    {49, "`"},
    {50, "}"},
}
local data_ru = {
    -- Russian strings
    {61, "А"},
    {62, "а"},
    {63, "Б"},
    {64, "б"},
    {65, "е"},
    {66, "её"},
    {67, "Её"},
    {68, "ЕЁ"},
    {69, "еЁ"},
    {70, "ёёё"},
    {71, "ё"},
    {72, "Ё"},
    {73, "ж"},
}

local data_combined = merge_tables(data_eng, data_num, data_symbols, data_ru)

--------------------------------------------
-----TEST CASES FOR DIFFERENT COLLATIONS----
--------------------------------------------

local data_test_binary_1 = {
    --   test_name , data to fill with, result output in col
    {"en", data_eng, {"AD","Aa","Ac","Ad","a","aE","aa","ab","aba","ae"}},
    {"num", data_num, {"0","1","2","21","23","3","9"}},
    {"symbols", data_symbols, {" ","!",")","/",":","<","@","[","`","}"}},
    {"ru", data_ru, {"Ё","А","Б","ЕЁ","Её","а","б","е","еЁ","её","ж","ё","ёёё"}},
    {"combined", data_combined,
        {" ","!",")","/","0","1","2","21","23","3","9",":","<","@",
            "AD","Aa","Ac","Ad","[","`","a","aE","aa","ab","aba","ae",
            "}","Ё","А","Б","ЕЁ","Её","а","б","е","еЁ","её","ж","ё","ёёё"}}
}

local data_test_unicode = {
    --   test_name , data to fill with, result output in col
    {"en", data_eng, {"a","aa","Aa","ab","aba","Ac","Ad","AD","ae","aE"}},
    {"num", data_num, {"0","1","2","21","23","3","9"}},
    {"symbols", data_symbols, {" ",":","!",")","[","}","@","/","`","<"}},
    {"ru", data_ru, {"а","А","б","Б","е","ё","Ё","её","еЁ","Её","ЕЁ","ёёё","ж"}},
    {"combined", data_combined,
        {" ",":","!",")","[","}","@","/","`","<","0","1","2","21","23",
            "3","9","a","aa","Aa","ab","aba","Ac","Ad","AD","ae","aE","а",
            "А","б","Б","е","ё","Ё","её","еЁ","Её","ЕЁ","ёёё","ж"}}
}


local data_test_unicode_ci = {
    --   test_name , data to fill with, result output in col
    {"en", data_eng, {"a","Aa","aa","ab","aba","Ac","Ad","AD","aE","ae"}},
    {"num", data_num, {"0","1","2","21","23","3","9"}},
    {"symbols", data_symbols, {" ",":","!",")","[","}","@","/","`","<"}},
    {"ru", data_ru, {"А","а","Б","б","е","ё","Ё","её","Её","ЕЁ","еЁ","ёёё","ж"}},
    {"combined", data_combined,
        {" ",":","!",")","[","}","@","/","`","<","0","1","2","21","23",
            "3","9","a","aa","Aa","ab","aba","Ac","Ad","AD","ae","aE","а",
            "А","б","Б","е","ё","Ё","её","еЁ","Её","ЕЁ","ёёё","ж"}}
}

local data_collations = {
    -- default collation = binary
    {"/*COLLATE DEFAULT*/", data_test_binary_1},
    {"COLLATE \"binary\"", data_test_binary_1},
    {"COLLATE \"unicode\"", data_test_unicode},
    {"COLLATE \"unicode_ci\"", data_test_unicode_ci},
}

for _, data_collation in ipairs(data_collations) do
    for _, test_case in ipairs(data_collation[2]) do
        local extendex_prefix = string.format("%s1.%s.%s.", prefix, data_collation[1], test_case[1])
        local data = test_case[2]
        local result = test_case[3]
        test:do_execsql_test(
            extendex_prefix.."create_table",
            string.format("create table t1(a INT primary key, b TEXT %s);", data_collation[1]),
            {})
        test:do_test(
            extendex_prefix.."insert_values",
            function()
                return insert_into_table("t1", data)
            end, {})
        test:do_execsql_test(
            extendex_prefix.."select_plan_contains_b-tree",
            string.format("explain query plan select b from t1 order by b %s;",data_collation[1]),
            {0,0,0,"SCAN TABLE T1 (~1048576 rows)",
                0,0,0,"USE TEMP B-TREE FOR ORDER BY"})
        test:do_execsql_test(
            extendex_prefix.."select",
            string.format("select b from t1 order by b %s;",data_collation[1]),
            result)
        test:do_execsql_test(
            extendex_prefix.."create index",
            string.format("create index i on t1(b %s)",data_collation[1]),
            {})
        test:do_execsql_test(
            extendex_prefix.."select_from_index_plan_does_not_contain_b-tree",
            string.format("explain query plan select b from t1 order by b %s;",data_collation[1]),
            {0,0,0,"SCAN TABLE T1 USING COVERING INDEX I (~1048576 rows)"})
        test:do_execsql_test(
            extendex_prefix.."select_from_index",
            string.format("select b from t1 order by b %s;",data_collation[1]),
            result)
        test:do_execsql_test(
            extendex_prefix.."drop_table",
            "drop table t1",
            {})
    end
end

-- <LIKE> uses collation. If <LIKE> has explicit <COLLATE>, use it
-- instead of implicit.
local like_testcases =
{
    {"2.0",
    [[
        CREATE TABLE tx1 (s1 VARCHAR(5) PRIMARY KEY COLLATE "unicode_ci");
        INSERT INTO tx1 VALUES('aaa');
        INSERT INTO tx1 VALUES('Aab');
        INSERT INTO tx1 VALUES('İac');
        INSERT INTO tx1 VALUES('iad');
    ]], {0}},
    {"2.1.1",
        "SELECT * FROM tx1 WHERE s1 LIKE 'A%' order by s1;",
        {0, {"aaa","Aab"}} },
    {"2.1.2",
        "EXPLAIN QUERY PLAN SELECT * FROM tx1 WHERE s1 LIKE 'A%';",
        {0, {0, 0, 0, "SEARCH TABLE TX1 USING PRIMARY KEY (S1>? AND S1<?) (~16384 rows)"}}},
    {"2.2.0",
        "SELECT * FROM tx1 WHERE s1 LIKE 'A%' COLLATE \"unicode\" order by s1;",
        {0, {"Aab"}} },
    {"2.2.1",
        "EXPLAIN QUERY PLAN SELECT * FROM tx1 WHERE s1 LIKE 'A%';",
        {0, {0, 0, 0, "/USING PRIMARY KEY/"}} },
    {"2.3.0",
        "SELECT * FROM tx1 WHERE s1 LIKE 'i%' order by s1;",
        {0, {"İac", "iad"}}},
    {"2.3.1",
        "SELECT * FROM tx1 WHERE s1 LIKE 'İ%' COLLATE \"unicode\" order by s1;",
        {0, {"İac"}} },
    {"2.4.0",
    [[
        INSERT INTO tx1 VALUES('ЯЁЮ');
    ]], {0} },
    {"2.4.1",
        "SELECT * FROM tx1 WHERE s1 LIKE 'яёю' COLLATE \"unicode\";",
        {0, {}} },
    {"2.4.2",
        "SELECT * FROM tx1 WHERE s1 COLLATE \"binary\" LIKE 'яёю';",
        {0, {}} },
    {"2.4.3",
        "SELECT * FROM tx1 WHERE s1 COLLATE \"binary\" LIKE 'яёю' COLLATE \"unicode\";",
        {1, "Illegal mix of collations"} },
    {"2.4.4",
        "SELECT * FROM tx1 WHERE s1 LIKE 'яёю';",
        {0, {"ЯЁЮ"}} },
}

test:do_execsql_test(
    "collation-2.6",
    [[
        CREATE TABLE tx3 (s1 VARCHAR(5) PRIMARY KEY COLLATE "unicode");
        INSERT INTO tx3 VALUES('aaa');
        INSERT INTO tx3 VALUES('Aab');
        SELECT s1 FROM tx3 WHERE s1 LIKE 'A%';
    ]], { 'Aab' })

test:do_catchsql_set_test(like_testcases, prefix)

test:do_catchsql_test(
        "collation-2.5.0",
        'CREATE TABLE test3 (a int, b int, c int, PRIMARY KEY (a, a COLLATE foo, b, c))',
        {1, "Collation 'FOO' does not exist"})

-- gh-3805 Check COLLATE passing with string-like args only.

test:do_execsql_test(
    "collation-2.7.0",
    [[ CREATE TABLE test1 (one INT PRIMARY KEY, two INT) ]],
    {})

test:do_catchsql_test(
    "collation-2.7.1",
    'SELECT one COLLATE "binary" FROM test1',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.2",
    'SELECT one COLLATE "unicode_ci" FROM test1',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.3",
    'SELECT two COLLATE "binary" FROM test1',
    {1, "COLLATE clause can't be used with non-string arguments"})


test:do_catchsql_test(
    "collation-2.7.4",
    'SELECT (one + two) COLLATE "binary" FROM test1',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.5",
    'SELECT (SELECT one FROM test1) COLLATE "binary"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_execsql_test(
    "collation-2.7.6",
    'SELECT TRIM(\'A\') COLLATE "binary"',
    {"A"})

test:do_catchsql_test(
    "collation-2.7.7",
    'SELECT RANDOM() COLLATE "binary"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.8",
    'SELECT LENGTH(\'A\') COLLATE "binary"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.9",
    'SELECT TRUE COLLATE "unicode"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.10",
    'SELECT NOT TRUE COLLATE "unicode"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.11",
    'SELECT TRUE AND TRUE COLLATE "unicode"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_catchsql_test(
    "collation-2.7.12",
    'SELECT 1 | 1 COLLATE "unicode"',
    {1, "COLLATE clause can't be used with non-string arguments"})

test:do_execsql_test(
    "collation-2.7.14",
    'SELECT +\'str\' COLLATE "unicode"',
    {"str"})

test:do_execsql_test(
    "collation-2.7.15",
    'SELECT (\'con\'||\'cat\') COLLATE "unicode"',
    {"concat"})

test:do_execsql_test(
    "collation-2.7.16",
    'SELECT (SELECT \'str\') COLLATE "binary" COLLATE "binary";',
    {"str"})

test:finish_test()
