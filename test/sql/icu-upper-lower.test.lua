test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

test_run:cmd("setopt delimiter ';'")

upper_lower_test = function (str)
    return box.execute(string.format("select lower('%s'), upper('%s')", str, str))
end;

-- Some pangrams
-- Azerbaijanian
upper_lower_test([[
    Zəfər, jaketini də, papağını da götür, bu axşam hava çox soyuq olacaq.
]]);
upper_lower_test([[
    The quick brown fox jumps over the lazy dog.
]]);
-- English
upper_lower_test([[
    The quick brown fox jumps over the lazy dog.
]]);
-- Armenian
upper_lower_test([[
    Բել դղյակի ձախ ժամն օֆ ազգությանը ցպահանջ չճշտած վնաս էր եւ փառք
]]);
-- Belarussian
upper_lower_test([[
    У Іўі худы жвавы чорт у зялёнай камізэльцы пабег пад’есці фаршу з юшкай
]]);
-- Greek
upper_lower_test([[
    Τάχιστη αλώπηξ βαφής ψημένη γη, δρασκελίζει υπέρ νωθρού κυνός
]]);
-- Irish
upper_lower_test([[
    Chuaigh bé mhórshách le dlúthspád fíorfhinn trí hata mo dhea-phorcáin bhig
]]);
-- Spain
upper_lower_test([[
    Quiere la boca exhausta vid, kiwi, piña y fugaz jamón
]]);
-- Korean
upper_lower_test([[
    키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다
]]);
-- Latvian
upper_lower_test([[
    Glāžšķūņa rūķīši dzērumā čiepj Baha koncertflīģeļu vākus
]]);
-- German
upper_lower_test([[
    Zwölf große Boxkämpfer jagen Viktor quer über den Sylter Deich
]]);
-- Polish
upper_lower_test([[
    Pchnąć w tę łódź jeża lub ośm skrzyń fig.
]]);
-- Ukrainian
upper_lower_test([[
    Чуєш їх, доцю, га? Кумедна ж ти, прощайся без ґольфів!
]]);
-- Czech
upper_lower_test([[
    Příliš žluťoučký kůň úpěl ďábelské ódy
]]);
-- Esperanto
upper_lower_test([[
    Laŭ Ludoviko Zamenhof bongustas freŝa ĉeĥa manĝaĵo kun spicoj
]]);
-- Japanese
upper_lower_test([[
    いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす
]]);
-- Turkish
upper_lower_test([[
    Pijamalı hasta yağız şoföre çabucak güvendi. EXTRA: İ
]]);

test_run:cmd("setopt delimiter ''");

-- Bad test cases
box.execute("select upper('1', 2)")
box.execute("select upper(\"1\")")
box.execute("select upper()")

