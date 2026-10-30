local lexer = require('internal.config.utils.expression_lexer')
local expression = require('internal.config.utils.expression')
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

-- Verify AST generation.
g.test_parse_basic = function()
    -- Verify operations priority and parentheses.
    t.assert_equals(expression.parse(
        '(a > b || c < d) && e <= 1.0.0 || 2.3.4 >= f || g != h && i == j'),
        {
            type = 'operation',
            value = '||',
            left = {
                type = 'operation',
                value = '||',
                left = {
                    type = 'operation',
                    value = '&&',
                    left = {
                        type = 'operation',
                        value = '||',
                        left = {
                            type = 'operation',
                            value = '>',
                            left = {
                                type = 'variable',
                                value = 'a',
                            },
                            right = {
                                type = 'variable',
                                value = 'b',
                            },
                        },
                        right = {
                            type = 'operation',
                            value = '<',
                            left = {
                                type = 'variable',
                                value = 'c',
                            },
                            right = {
                                type = 'variable',
                                value = 'd',
                            },
                        },
                    },
                    right = {
                        type = 'operation',
                        value = '<=',
                        left = {
                            type = 'variable',
                            value = 'e',
                        },
                        right = {
                            type = 'version_literal',
                            value = '1.0.0',
                        },
                    },
                },
                right = {
                    type = 'operation',
                    value = '>=',
                    left = {
                        type = 'version_literal',
                        value = '2.3.4',
                    },
                    right = {
                        type = 'variable',
                        value = 'f',
                    },
                },
            },
            right = {
                type = 'operation',
                value = '&&',
                left = {
                    type = 'operation',
                    value = '!=',
                    left = {
                        type = 'variable',
                        value = 'g',
                    },
                    right = {
                        type = 'variable',
                        value = 'h',
                    },
                },
                right = {
                    type = 'operation',
                    value = '==',
                    left = {
                        type = 'variable',
                        value = 'i',
                    },
                    right = {
                        type = 'variable',
                        value = 'j',
                    },
                },
            },
        })
    -- A closing parenthesis is correctly consumed as an end of
    -- an inner sub-expression.
    t.assert_equals(expression.parse('((((1.0.0))))'), {
        type = 'version_literal',
        value = '1.0.0',
    })
    -- Left associativity of && and ||.
    --
    -- The comparison operations are also left associative, but
    -- it doesn't matter. Sespite that `a >= b >= c` parses
    -- successfully, such expressions don't pass a validation
    -- stage.
    t.assert_equals(expression.parse('a > b && b > c && c > d'), {
        type = 'operation',
        value = '&&',
        left = {
            type = 'operation',
            value = '&&',
            left = {
                type = 'operation',
                value = '>',
                left = {
                    type = 'variable',
                    value = 'a',
                },
                right = {
                    type = 'variable',
                    value = 'b',
                },
            },
            right = {
                type = 'operation',
                value = '>',
                left = {
                    type = 'variable',
                    value = 'b',
                },
                right = {
                    type = 'variable',
                    value = 'c',
                },
            },
        },
        right = {
            type = 'operation',
            value = '>',
            left = {
                type = 'variable',
                value = 'c',
            },
            right = {
                type = 'variable',
                value = 'd',
            },
        },
    })
    t.assert_equals(expression.parse('a > b || b > c || c > d'), {
        type = 'operation',
        value = '||',
        left = {
            type = 'operation',
            value = '||',
            left = {
                type = 'operation',
                value = '>',
                left = {
                    type = 'variable',
                    value = 'a',
                },
                right = {
                    type = 'variable',
                    value = 'b',
                },
            },
            right = {
                type = 'operation',
                value = '>',
                left = {
                    type = 'variable',
                    value = 'b',
                },
                right = {
                    type = 'variable',
                    value = 'c',
                },
            },
        },
        right = {
            type = 'operation',
            value = '>',
            left = {
                type = 'variable',
                value = 'c',
            },
            right = {
                type = 'variable',
                value = 'd',
            },
        },
    })
end

-- A couple of incorrect expressions.
g.test_parse_failure = function()
    t.assert_error_msg_equals('Expected a string as an expression, got nil',
        expression.parse)
    t.assert_error_msg_equals('Expected a string as an expression, got number',
        expression.parse, 1)
    t.assert_error_msg_equals('Unexpected end of an expression',
        expression.parse, '')
    t.assert_error_msg_equals('Unexpected end of an expression',
        expression.parse, '(4.0.0 <')
    t.assert_error_msg_equals('Expected ")", got end of an expression',
        expression.parse, '(4.0.0 < 5.0.0')
    t.assert_error_msg_equals('Expected an operation, got "6.0.0"',
        expression.parse, '(4.0.0 < 5.0.0 6.0.0')
    t.assert_error_msg_equals('Expected an operation, got "("',
        expression.parse, '(4.0.0 < 5.0.0 (')
    t.assert_error_msg_equals('Unexpected token ">"',
        expression.parse, '>')
    t.assert_error_msg_equals('Expected end of an expression, got ")"',
        expression.parse, '5.0.0 < 6.0.0)')
end

g.test_validate_success = function()
    local v = '1.0.0'
    local vars = {a = v, b = v, c = v, d = v, e = v, f = v, g = v, h = v, i = v,
        j = v}
    local ast = expression.parse(
        '(a > b || c < d) && e <= 1.0.0 || 2.3.4 >= f || g != h && i == j')
    expression.validate(ast, vars)
end

g.test_validate_failure = function()
    local ast = expression.parse('a')
    local exp_err = 'An expression should be a predicate, got variable'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local ast = expression.parse('1.0.0')
    local exp_err = 'An expression should be a predicate, got version_literal'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local ast = expression.parse('1.0.0 < 2.0.0 < 3.0.0')
    local exp_err = 'A comparison operator (<, >, <=, >=, !=, ==) requires ' ..
        'version literals or variables as arguments'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local ast = expression.parse('1.0.0 == 2.0.0 == 3.0.0')
    local exp_err = 'A comparison operator (<, >, <=, >=, !=, ==) requires ' ..
        'version literals or variables as arguments'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local ast = expression.parse('1.0.0 != 2.0.0 != 3.0.0')
    local exp_err = 'A comparison operator (<, >, <=, >=, !=, ==) requires ' ..
        'version literals or variables as arguments'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local ast = expression.parse('x && 2.0.0')
    local exp_err = 'A logical operator (&& or ||) accepts only boolean ' ..
        'expressions as arguments'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local ast = expression.parse('x > 0.0.0')
    local exp_err = 'Unknown variable: "x"'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, {})

    local vars = {x = 1}
    local ast = expression.parse('x > 0.0.0')
    local exp_err = 'Variable "x" has type number, expected string'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, vars)

    local vars = {x = ''}
    local ast = expression.parse('x > 0.0.0')
    local exp_err = 'Expected a version in variable "x", got an empty string'
    t.assert_error_msg_equals(exp_err, expression.validate, ast, vars)

    for _, x in ipairs({'.1', '1.', 'x', ' 1', '1 ', '1..2'}) do
        local vars = {x = x}
        local ast = expression.parse('x > 0.0.0')
        local exp_err = 'Variable "x" is not a version string'
        t.assert_error_msg_equals(exp_err, expression.validate, ast, vars)
    end

    for _, x in ipairs({'1', '1.2', '1.2.3.4', '1.2.3.4.5'}) do
        local vars = {x = x}
        local ast = expression.parse('x > 0.0.0')
        local exp_err = ('Expected a three component version in variable ' ..
            '"x", got %d components'):format((#x + 1) / 2)
        t.assert_error_msg_equals(exp_err, expression.validate, ast, vars)
    end
end

g.test_evaluate = function()
    local function eval(s, vars)
        local ast = expression.parse(s)
        expression.validate(ast, vars)
        local res_1 = expression.evaluate(ast, vars)
        local res_2 = expression.eval(s, vars)
        t.assert_equals(res_1, res_2)
        return res_1
    end

    t.assert_equals(eval('v > 1.0.0', {v = '0.0.1'}), false)
    t.assert_equals(eval('v > 1.0.0', {v = '1.0.0'}), false)
    t.assert_equals(eval('v > 1.0.0', {v = '1.0.1'}), true)
    t.assert_equals(eval('v > 1.0.0', {v = '2.0.0'}), true)

    t.assert_equals(eval('v < 1.0.0', {v = '0.0.1'}), true)
    t.assert_equals(eval('v < 1.0.0', {v = '1.0.0'}), false)
    t.assert_equals(eval('v < 1.0.0', {v = '1.0.1'}), false)
    t.assert_equals(eval('v < 1.0.0', {v = '2.0.0'}), false)

    t.assert_equals(eval('v >= 1.0.0', {v = '0.0.1'}), false)
    t.assert_equals(eval('v >= 1.0.0', {v = '1.0.0'}), true)
    t.assert_equals(eval('v >= 1.0.0', {v = '1.0.1'}), true)
    t.assert_equals(eval('v >= 1.0.0', {v = '2.0.0'}), true)

    t.assert_equals(eval('v <= 1.0.0', {v = '0.0.1'}), true)
    t.assert_equals(eval('v <= 1.0.0', {v = '1.0.0'}), true)
    t.assert_equals(eval('v <= 1.0.0', {v = '1.0.1'}), false)
    t.assert_equals(eval('v <= 1.0.0', {v = '2.0.0'}), false)

    t.assert_equals(eval('v == 1.0.0', {v = '0.0.1'}), false)
    t.assert_equals(eval('v == 1.0.0', {v = '1.0.0'}), true)
    t.assert_equals(eval('v == 1.0.0', {v = '1.0.1'}), false)
    t.assert_equals(eval('v == 1.0.0', {v = '2.0.0'}), false)

    t.assert_equals(eval('v != 1.0.0', {v = '0.0.1'}), true)
    t.assert_equals(eval('v != 1.0.0', {v = '1.0.0'}), false)
    t.assert_equals(eval('v != 1.0.0', {v = '1.0.1'}), true)
    t.assert_equals(eval('v != 1.0.0', {v = '2.0.0'}), true)

    local expr = 'v >= 3.1.0 && v < 3.2.0 || v >= 4.0.0'
    t.assert_equals(eval(expr, {v = '3.0.0'}), false)
    t.assert_equals(eval(expr, {v = '3.1.0'}), true)
    t.assert_equals(eval(expr, {v = '3.1.1'}), true)
    t.assert_equals(eval(expr, {v = '3.2.0'}), false)
    t.assert_equals(eval(expr, {v = '4.0.0'}), true)
    t.assert_equals(eval(expr, {v = '4.1.0'}), true)
    t.assert_equals(eval(expr, {v = '5.1.0'}), true)

    t.assert_equals(eval('1.10.0 > 1.2.0', {}), true)
end
