import sql_ast
import re

object_no_re = re.compile("[a-z_]*", re.I)

%%

parser sql:

    ignore:           '\\s+'
    token NUM:        '[+-]?[0-9]+'
    token ID:         '[a-z_]+[0-9]+' 
    token STR:        '\'([^\']+|\\\\.)*\''
    token PING:       'ping'
    token INSERT:     'insert'
    token UPDATE:     'update'
    token DELETE:     'delete'
    token SELECT:     'select'
    token INTO:       'into'
    token FROM:       'from'
    token WHERE:      'where'
    token VALUES:     'values'
    token SET:        'set'
    token END:        '\\s*$'

    rule sql:         (insert {{ stmt = insert }} |
                      update {{ stmt = update }} |
                      delete {{ stmt = delete }} |
                      select {{ stmt = select }} |
                      ping {{ stmt = ping }}) END {{ return stmt }}
                      
    rule insert:      INSERT [INTO] ident VALUES value_list
                      {{ return sql_ast.StatementInsert(ident, value_list) }}
    rule update:      UPDATE ident SET update_list opt_where 
                      {{ return sql_ast.StatementUpdate(ident, update_list, opt_where) }}
    rule delete:      DELETE FROM ident opt_where
                      {{ return sql_ast.StatementDelete(ident, opt_where) }}
    rule select:      SELECT '\*' FROM ident opt_where
                      {{ return sql_ast.StatementSelect(ident, opt_where) }}
    rule ping:        PING
                      {{ return sql_ast.StatementPing() }}
    rule predicate:   ident '=' constant
                      {{ return (ident, constant) }}
    rule opt_where:   {{ return None }}
                      | WHERE predicate
                      {{ return predicate }}
    rule value_list:  '\(' expr {{ value_list = [expr] }}
                          [("," expr {{ value_list.append(expr) }} )+]
                      '\)' {{ return value_list }}
    rule update_list: predicate {{ update_list = [predicate] }}
                      [(',' predicate {{ update_list.append(predicate) }})+]
                      {{ return update_list }}
    rule expr:        constant {{ return constant }}
    rule constant:    NUM {{ return int(NUM) }} | STR {{ return STR[1:-1] }}
    rule ident:       ID {{ return int(object_no_re.sub("", ID)) }}
%%

# SQL is case-insensitive, but in yapps it's not possible to
# specify that a token must match in case-insensitive fashion.
# This is hack to add re.IGNORECASE flag to all regular
# expressions that represent tokens in the generated grammar.

sqlScanner.patterns = map(lambda tup:
                          (tup[0], re.compile(tup[1].pattern, re.IGNORECASE)),
                          sqlScanner.patterns)

# vim: nospell syntax=off ts=4 et

