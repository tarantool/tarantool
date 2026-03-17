local server = require('luatest.server')
local t = require('luatest')

local g = t.group("upper_lower")

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_upper_lower = function(cg)
    cg.server:exec(function()
        local upper_lower_test = function (str)
            return box.execute(string.format("SELECT LOWER('%s'), UPPER('%s');",
                                             str, str))
        end
        -- Some pangrams.
        -- Azerbaijanian.
        local str = "Zəfər, jaketini də, papağını da götür, " ..
                    "bu axşam hava çox soyuq olacaq."
        local res = upper_lower_test(str)
        local exp = {{'zəfər, jaketini də, papağını da götür, ' ..
                      'bu axşam hava çox soyuq olacaq.',
                      'ZƏFƏR, JAKETINI DƏ, PAPAĞINI DA GÖTÜR, ' ..
                      'BU AXŞAM HAVA ÇOX SOYUQ OLACAQ.'}}
        t.assert_equals(res.rows, exp)
        -- English.
        str = [[The quick brown fox jumps over the lazy dog.]]
        res = upper_lower_test(str)
        exp = {{'the quick brown fox jumps over the lazy dog.',
                'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.'}}
        t.assert_equals(res.rows, exp)
        -- Armenian.
        str = "Բել դղյակի ձախ ժամն օֆ ազգությանը ցպահանջ չճշտած վնաս էր եւ փառք"
        res = upper_lower_test(str)
        exp = {{'բել դղյակի ձախ ժամն օֆ ազգությանը ' ..
                'ցպահանջ չճշտած վնաս էր եւ փառք',
                'ԲԵԼ ԴՂՅԱԿԻ ՁԱԽ ԺԱՄՆ ՕՖ ԱԶԳՈՒԹՅԱՆԸ ' ..
                'ՑՊԱՀԱՆՋ ՉՃՇՏԱԾ ՎՆԱՍ ԷՐ ԵՒ ՓԱՌՔ'}}
        t.assert_equals(res.rows, exp)
        -- Belarussian.
        str = [[У Іўі худы жвавы чорт у зялёнай камізэльцы ]] ..
              [[пабег пад’есці фаршу з юшкай]]
        res = upper_lower_test(str)
        exp = {{'у іўі худы жвавы чорт у зялёнай камізэльцы ' ..
                'пабег пад’есці фаршу з юшкай',
                'У ІЎІ ХУДЫ ЖВАВЫ ЧОРТ У ЗЯЛЁНАЙ КАМІЗЭЛЬЦЫ ' ..
                'ПАБЕГ ПАД’ЕСЦІ ФАРШУ З ЮШКАЙ'}}
        t.assert_equals(res.rows, exp)
        -- Greek.
        str = [[Τάχιστη αλώπηξ βαφής ψημένη γη, δρασκελίζει υπέρ νωθρού κυνός]]
        res = upper_lower_test(str)
        exp = {{'τάχιστη αλώπηξ βαφής ψημένη γη, ' ..
                'δρασκελίζει υπέρ νωθρού κυνός',
                'ΤΆΧΙΣΤΗ ΑΛΏΠΗΞ ΒΑΦΉΣ ΨΗΜΈΝΗ ΓΗ, ' ..
                'ΔΡΑΣΚΕΛΊΖΕΙ ΥΠΈΡ ΝΩΘΡΟΎ ΚΥΝΌΣ'}}
        t.assert_equals(res.rows, exp)
        -- Irish.
        str = [[Chuaigh bé mhórshách le dlúthspád fíorfhinn ]] ..
              [[trí hata mo dhea-phorcáin bhig]]
        res = upper_lower_test(str)
        exp = {{'chuaigh bé mhórshách le dlúthspád fíorfhinn ' ..
                'trí hata mo dhea-phorcáin bhig',
                'CHUAIGH BÉ MHÓRSHÁCH LE DLÚTHSPÁD FÍORFHINN ' ..
                'TRÍ HATA MO DHEA-PHORCÁIN BHIG'}}
        t.assert_equals(res.rows, exp)
        -- Spain.
        str = [[Quiere la boca exhausta vid, kiwi, piña y fugaz jamón]]
        res = upper_lower_test(str)
        exp = {{'quiere la boca exhausta vid, kiwi, piña y fugaz jamón',
                'QUIERE LA BOCA EXHAUSTA VID, KIWI, PIÑA Y FUGAZ JAMÓN'}}
        t.assert_equals(res.rows, exp)
        -- Korean.
        str = [[키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다]]
        res = upper_lower_test(str)
        exp = {{'키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다',
                '키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다'}}
        t.assert_equals(res.rows, exp)
        -- Latvian.
        str = [[Glāžšķūņa rūķīši dzērumā čiepj Baha koncertflīģeļu vākus]]
        res = upper_lower_test(str)
        exp = {{'glāžšķūņa rūķīši dzērumā čiepj baha koncertflīģeļu vākus',
                'GLĀŽŠĶŪŅA RŪĶĪŠI DZĒRUMĀ ČIEPJ BAHA KONCERTFLĪĢEĻU VĀKUS'}}
        t.assert_equals(res.rows, exp)
        -- German.
        str = [[Zwölf große Boxkämpfer jagen Viktor quer über den Sylter Deich]]
        res = upper_lower_test(str)
        exp = {{'zwölf große boxkämpfer jagen viktor ' ..
                'quer über den sylter deich',
                'ZWÖLF GROSSE BOXKÄMPFER JAGEN VIKTOR ' ..
                'QUER ÜBER DEN SYLTER DEICH'}}
        t.assert_equals(res.rows, exp)
        -- Polish.
        str = [[Pchnąć w tę łódź jeża lub ośm skrzyń fig.]]
        res = upper_lower_test(str)
        exp = {{'pchnąć w tę łódź jeża lub ośm skrzyń fig.',
                'PCHNĄĆ W TĘ ŁÓDŹ JEŻA LUB OŚM SKRZYŃ FIG.'}}
        t.assert_equals(res.rows, exp)
        -- Ukrainian.
        str = [[Чуєш їх, доцю, га? Кумедна ж ти, прощайся без ґольфів!]]
        res = upper_lower_test(str)
        exp = {{'чуєш їх, доцю, га? кумедна ж ти, прощайся без ґольфів!',
                'ЧУЄШ ЇХ, ДОЦЮ, ГА? КУМЕДНА Ж ТИ, ПРОЩАЙСЯ БЕЗ ҐОЛЬФІВ!'}}
        t.assert_equals(res.rows, exp)
        -- Czech.
        str = [[Příliš žluťoučký kůň úpěl ďábelské ódy]]
        res = upper_lower_test(str)
        exp = {{'příliš žluťoučký kůň úpěl ďábelské ódy',
                'PŘÍLIŠ ŽLUŤOUČKÝ KŮŇ ÚPĚL ĎÁBELSKÉ ÓDY'}}
        t.assert_equals(res.rows, exp)
        -- Esperanto.
        str = [[Laŭ Ludoviko Zamenhof bongustas freŝa ĉeĥa manĝaĵo kun spicoj]]
        res = upper_lower_test(str)
        exp = {{'laŭ ludoviko zamenhof bongustas freŝa ' ..
                'ĉeĥa manĝaĵo kun spicoj',
                'LAŬ LUDOVIKO ZAMENHOF BONGUSTAS FREŜA ' ..
                'ĈEĤA MANĜAĴO KUN SPICOJ'}}
        t.assert_equals(res.rows, exp)
        -- Japanese.
        str = [[いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす]]
        res = upper_lower_test(str)
        exp = {{'いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす',
                'いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす'}}
        t.assert_equals(res.rows, exp)
        -- Turkish.
        str = [[Pijamalı hasta yağız şoföre çabucak güvendi. EXTRA: İ]]
        res = upper_lower_test(str)
        exp = {{'pijamalı hasta yağız şoföre çabucak güvendi. extra: i̇',
                'PIJAMALI HASTA YAĞIZ ŞOFÖRE ÇABUCAK GÜVENDI. EXTRA: İ'}}
        t.assert_equals(res.rows, exp)

        -- Bad test cases.
        local _, err = box.execute("SELECT UPPER('1', 2);")
        local exp_err = "Wrong number of arguments is passed to UPPER(): " ..
                        "expected 1, got 2"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute('SELECT UPPER("1");')
        t.assert_equals(err.message, "Can't resolve field '1'")
        _, err = box.execute("SELECT UPPER();")
        exp_err = "Wrong number of arguments is passed to UPPER(): " ..
                  "expected 1, got 0"
        t.assert_equals(err.message, exp_err)
    end)
end
