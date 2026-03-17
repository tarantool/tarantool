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
        local upper_lower_test = function(str, exp)
            local sql = "SELECT LOWER('%s'), UPPER('%s');"
            t.assert_equals(box.execute(sql:format(str, str)).rows, exp)
        end
        -- Some pangrams.
        -- Azerbaijanian.
        local str = "Zəfər, pencəyini və papağını götür, bu gecə çox soyuq " ..
                    "olacaq."
        local exp = {{'zəfər, pencəyini və papağını götür, ' ..
                      'bu gecə çox soyuq olacaq.',
                      'ZƏFƏR, PENCƏYINI VƏ PAPAĞINI GÖTÜR, ' ..
                      'BU GECƏ ÇOX SOYUQ OLACAQ.'}}
        upper_lower_test(str, exp)
        -- English.
        str = [[The quick brown fox jumps over the lazy dog.]]
        exp = {{'the quick brown fox jumps over the lazy dog.',
                'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.'}}
        upper_lower_test(str, exp)
        -- Armenian.
        str = "Բել դղյակի ձախ ժամն օֆ ազգությանը ցպահանջ չճշտած վնաս էր եւ փառք"
        exp = {{'բել դղյակի ձախ ժամն օֆ ազգությանը ' ..
                'ցպահանջ չճշտած վնաս էր եւ փառք',
                'ԲԵԼ ԴՂՅԱԿԻ ՁԱԽ ԺԱՄՆ ՕՖ ԱԶԳՈՒԹՅԱՆԸ ' ..
                'ՑՊԱՀԱՆՋ ՉՃՇՏԱԾ ՎՆԱՍ ԷՐ ԵՒ ՓԱՌՔ'}}
        upper_lower_test(str, exp)
        -- Belarussian.
        str = [[У Іўі худы жвавы чорт у зялёнай камізэльцы ]] ..
              [[пабег пад’есці фаршу з юшкай]]
        exp = {{'у іўі худы жвавы чорт у зялёнай камізэльцы ' ..
                'пабег пад’есці фаршу з юшкай',
                'У ІЎІ ХУДЫ ЖВАВЫ ЧОРТ У ЗЯЛЁНАЙ КАМІЗЭЛЬЦЫ ' ..
                'ПАБЕГ ПАД’ЕСЦІ ФАРШУ З ЮШКАЙ'}}
        upper_lower_test(str, exp)
        -- Greek.
        str = [[Τάχιστη αλώπηξ βαφής ψημένη γη, δρασκελίζει υπέρ νωθρού κυνός]]
        exp = {{'τάχιστη αλώπηξ βαφής ψημένη γη, ' ..
                'δρασκελίζει υπέρ νωθρού κυνός',
                'ΤΆΧΙΣΤΗ ΑΛΏΠΗΞ ΒΑΦΉΣ ΨΗΜΈΝΗ ΓΗ, ' ..
                'ΔΡΑΣΚΕΛΊΖΕΙ ΥΠΈΡ ΝΩΘΡΟΎ ΚΥΝΌΣ'}}
        upper_lower_test(str, exp)
        -- Irish.
        str = [[Chuaigh bé mhórshách le dlúthspád fíorfhinn ]] ..
              [[trí hata mo dhea-phorcáin bhig]]
        exp = {{'chuaigh bé mhórshách le dlúthspád fíorfhinn ' ..
                'trí hata mo dhea-phorcáin bhig',
                'CHUAIGH BÉ MHÓRSHÁCH LE DLÚTHSPÁD FÍORFHINN ' ..
                'TRÍ HATA MO DHEA-PHORCÁIN BHIG'}}
        upper_lower_test(str, exp)
        -- Spain.
        str = [[Quiere la boca exhausta vid, kiwi, piña y fugaz jamón]]
        exp = {{'quiere la boca exhausta vid, kiwi, piña y fugaz jamón',
                'QUIERE LA BOCA EXHAUSTA VID, KIWI, PIÑA Y FUGAZ JAMÓN'}}
        upper_lower_test(str, exp)
        -- Korean.
        str = [[키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다]]
        exp = {{'키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다',
                '키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다'}}
        upper_lower_test(str, exp)
        -- Latvian.
        str = [[Glāžšķūņa rūķīši dzērumā čiepj Baha koncertflīģeļu vākus]]
        exp = {{'glāžšķūņa rūķīši dzērumā čiepj baha koncertflīģeļu vākus',
                'GLĀŽŠĶŪŅA RŪĶĪŠI DZĒRUMĀ ČIEPJ BAHA KONCERTFLĪĢEĻU VĀKUS'}}
        upper_lower_test(str, exp)
        -- German.
        str = [[Zwölf große Boxkämpfer jagen Viktor quer über den Sylter Deich]]
        exp = {{'zwölf große boxkämpfer jagen viktor ' ..
                'quer über den sylter deich',
                'ZWÖLF GROSSE BOXKÄMPFER JAGEN VIKTOR ' ..
                'QUER ÜBER DEN SYLTER DEICH'}}
        upper_lower_test(str, exp)
        -- Polish.
        str = [[Pchnąć w tę łódź jeża lub ośm skrzyń fig.]]
        exp = {{'pchnąć w tę łódź jeża lub ośm skrzyń fig.',
                'PCHNĄĆ W TĘ ŁÓDŹ JEŻA LUB OŚM SKRZYŃ FIG.'}}
        upper_lower_test(str, exp)
        -- Ukrainian.
        str = [[Чуєш їх, доцю, га? Кумедна ж ти, прощайся без ґольфів!]]
        exp = {{'чуєш їх, доцю, га? кумедна ж ти, прощайся без ґольфів!',
                'ЧУЄШ ЇХ, ДОЦЮ, ГА? КУМЕДНА Ж ТИ, ПРОЩАЙСЯ БЕЗ ҐОЛЬФІВ!'}}
        upper_lower_test(str, exp)
        -- Czech.
        str = [[Příliš žluťoučký kůň úpěl ďábelské ódy]]
        exp = {{'příliš žluťoučký kůň úpěl ďábelské ódy',
                'PŘÍLIŠ ŽLUŤOUČKÝ KŮŇ ÚPĚL ĎÁBELSKÉ ÓDY'}}
        upper_lower_test(str, exp)
        -- Esperanto.
        str = [[Laŭ Ludoviko Zamenhof bongustas freŝa ĉeĥa manĝaĵo kun spicoj]]
        exp = {{'laŭ ludoviko zamenhof bongustas freŝa ' ..
                'ĉeĥa manĝaĵo kun spicoj',
                'LAŬ LUDOVIKO ZAMENHOF BONGUSTAS FREŜA ' ..
                'ĈEĤA MANĜAĴO KUN SPICOJ'}}
        upper_lower_test(str, exp)
        -- Japanese.
        str = [[いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす]]
        exp = {{'いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす',
                'いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす'}}
        upper_lower_test(str, exp)
        -- Turkish.
        str = [[Pijamalı hasta yağız şoföre çabucak güvendi. EXTRA: İ]]
        exp = {{'pijamalı hasta yağız şoföre çabucak güvendi. extra: i̇',
                'PIJAMALI HASTA YAĞIZ ŞOFÖRE ÇABUCAK GÜVENDI. EXTRA: İ'}}
        upper_lower_test(str, exp)

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
