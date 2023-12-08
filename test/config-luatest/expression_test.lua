local lexer = require('internal.config.utils.expression_lexer')
local t = require('luatest')

local g = t.group()

g.test_lexer = function()
    local function exp_err(line, column, msg)
        return ('Expression parsing error at line %d, column %d: %s'):format(
            line, column, msg)
    end

    -- Operators are separators, spaces around are not required.
    --
    -- Variables are accepted.
    --
    -- >= and < are the operators.
    t.assert_equals(lexer.split('1.0.0>=2.0.0'), {
        {type = 'version_literal', value = '1.0.0'},
        {type = 'operation', value = '>='},
        {type = 'version_literal', value = '2.0.0'},
    })
    t.assert_equals(lexer.split('x<2.0.0'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '<'},
        {type = 'version_literal', value = '2.0.0'},
    })
    t.assert_equals(lexer.split('x < 2.0.0'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '<'},
        {type = 'version_literal', value = '2.0.0'},
    })

    -- Version literals are accepted.
    --
    -- <= and > are the operators.
    t.assert_equals(lexer.split('1.0.0<=2.3.4'), {
        {type = 'version_literal', value = '1.0.0'},
        {type = 'operation', value = '<='},
        {type = 'version_literal', value = '2.3.4'},
    })
    t.assert_equals(lexer.split('x>2.7.0'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '>'},
        {type = 'version_literal', value = '2.7.0'},
    })
    t.assert_equals(lexer.split('x > 2.7.0'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '>'},
        {type = 'version_literal', value = '2.7.0'},
    })

    -- Variables are separared by operators too, not only
    -- literals.
    --
    -- == and != are the operators (and separators).
    t.assert_equals(lexer.split('x==y'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '=='},
        {type = 'variable', value = 'y'},
    })
    t.assert_equals(lexer.split('x!=y'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '!='},
        {type = 'variable', value = 'y'},
    })
    t.assert_equals(lexer.split('x != y'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '!='},
        {type = 'variable', value = 'y'},
    })

    -- && and || are the operators too.
    t.assert_equals(lexer.split('x < y &&a >= b ||c>d'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '<'},
        {type = 'variable', value = 'y'},
        {type = 'operation', value = '&&'},
        {type = 'variable', value = 'a'},
        {type = 'operation', value = '>='},
        {type = 'variable', value = 'b'},
        {type = 'operation', value = '||'},
        {type = 'variable', value = 'c'},
        {type = 'operation', value = '>'},
        {type = 'variable', value = 'd'},
    })

    -- No problems with >=, >, <=, <, !=, == at the end of the
    -- string.
    t.assert_equals(lexer.split('x>='), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '>='},
    })
    t.assert_equals(lexer.split('x>'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '>'},
    })
    t.assert_equals(lexer.split('x<='), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '<='},
    })
    t.assert_equals(lexer.split('x<'), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '<'},
    })
    t.assert_equals(lexer.split('x!='), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '!='},
    })
    t.assert_equals(lexer.split('x=='), {
        {type = 'variable', value = 'x'},
        {type = 'operation', value = '=='},
    })

    -- Parentheses are the valid tokens and they're separators
    -- too.
    t.assert_equals(lexer.split('(a > b || c < d) && e <= f'), {
        {type = 'grouping', value = '('},
        {type = 'variable', value = 'a'},
        {type = 'operation', value = '>'},
        {type = 'variable', value = 'b'},
        {type = 'operation', value = '||'},
        {type = 'variable', value = 'c'},
        {type = 'operation', value = '<'},
        {type = 'variable', value = 'd'},
        {type = 'grouping', value = ')'},
        {type = 'operation', value = '&&'},
        {type = 'variable', value = 'e'},
        {type = 'operation', value = '<='},
        {type = 'variable', value = 'f'},
    })

    -- A variable name can contain an underscore.
    --
    -- It also can start from an underscore.
    t.assert_equals(lexer.split('foo_bar'), {
        {type = 'variable', value = 'foo_bar'},
    })
    t.assert_equals(lexer.split('_foo'), {
        {type = 'variable', value = '_foo'},
    })

    -- Spaces at start, spaces at end.
    t.assert_equals(lexer.split(' 1.0.0'), {
        {type = 'version_literal', value = '1.0.0'},
    })
    t.assert_equals(lexer.split('1.0.0 '), {
        {type = 'version_literal', value = '1.0.0'},
    })

    -- &<eof> and |<eof> are forbidden.
    t.assert_error_msg_equals(exp_err(1, 3, 'truncated expression'),
        lexer.split, 'x&')
    t.assert_error_msg_equals(exp_err(1, 3, 'truncated expression'),
        lexer.split, 'x|')

    -- &| and |& are forbidden.
    t.assert_error_msg_equals(exp_err(1, 2, 'invalid token'),
        lexer.split, '&|')
    t.assert_error_msg_equals(exp_err(1, 2, 'invalid token'),
        lexer.split, '|&')

    -- !<eof> and =<eof> are forbidden.
    t.assert_error_msg_equals(exp_err(1, 3, 'truncated expression'),
        lexer.split, 'x!')
    t.assert_error_msg_equals(exp_err(1, 3, 'truncated expression'),
        lexer.split, 'x=')

    -- =x and !x are forbidden.
    t.assert_error_msg_equals(exp_err(1, 2, 'invalid token'),
        lexer.split, '!x')
    t.assert_error_msg_equals(exp_err(1, 2, 'invalid token'),
        lexer.split, '=x')

    -- A separator should follow a version literal or a variable.
    t.assert_error_msg_equals(exp_err(1, 6, 'invalid token'),
        lexer.split, '0.1.2x')
    t.assert_error_msg_equals(exp_err(1, 2, 'invalid token'),
        lexer.split, 'x.')

    -- | and & are invalid operators.
    t.assert_error_msg_equals(exp_err(1, 3, 'invalid token'),
        lexer.split, 'a&b')
    t.assert_error_msg_equals(exp_err(1, 3, 'invalid token'),
        lexer.split, 'a|b')

    -- A version literal doesn't end with a full stop.
    t.assert_error_msg_equals(exp_err(1, 3, 'invalid token'),
        lexer.split, '3.')
    t.assert_error_msg_equals(exp_err(1, 3, 'invalid token'),
        lexer.split, '3..4')
    t.assert_error_msg_equals(exp_err(1, 3, 'invalid token'),
        lexer.split, '3.>x')

    -- A version literal contains only three components.
    t.assert_error_msg_equals(exp_err(1, 2, 'invalid version literal: ' ..
        'expected 3 components, got 1'),
        lexer.split, '1')
    t.assert_error_msg_equals(exp_err(1, 4, 'invalid version literal: ' ..
        'expected 3 components, got 2'),
        lexer.split, '1.2')
    t.assert_error_msg_equals(exp_err(1, 8, 'invalid version literal: ' ..
        'expected 3 components, got 4'),
        lexer.split, '1.2.3.4')
end
