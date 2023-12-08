-- Parse C style expression.
--
-- The module implements Pratt's expression parsing algorithm.
--
-- The functionality is quite limited, because the module is
-- internal and is used for one specific task: process tarantool
-- version constraints in the declarative configuration module.
-- (`conditional[N].if`).
--
-- The module support one data type: version. A value may be
-- referenced in two ways:
--
-- 1. Version literal: 1.2.3 (always three components).
-- 2. Variable: foo, x23, _foo_bar and so.
--
-- The operations are the following.
--
-- 1. Logical OR: ||
-- 2. Logical AND: &&
-- 3. Compare: >, <, >=, <=, !=, ==
-- 4. Parentheses: (, )

local expression_lexer = require('internal.config.utils.expression_lexer')

-- NB: bpower is the abbreviation a for binding power.
local left_bpower_map = {
    ['||'] = 1,
    ['&&'] = 3,
    ['<'] = 5,
    ['>'] = 5,
    ['<='] = 5,
    ['>='] = 5,
    ['!='] = 5,
    ['=='] = 5,
}

local right_bpower_map = {
    ['||'] = 2,
    ['&&'] = 4,
    ['<'] = 6,
    ['>'] = 6,
    ['<='] = 6,
    ['>='] = 6,
    ['!='] = 6,
    ['=='] = 6,
}

local function parse_expr(lexer, min_bpower)
    local left

    local token = lexer:next()
    if token == nil then
        error('Unexpected end of an expression', 0)
    elseif token.type == 'version_literal' then
        left = token
    elseif token.type == 'variable' then
        left = token
    elseif token.value == '(' then
        left = parse_expr(lexer, 0)
        token = lexer:next()
        if token == nil then
            error('Expected ")", got end of an expression', 0)
        end
        -- All the exits from the recursive parse_expr() call with
        -- zero min_bpower are either with ')' or if there are no
        -- more tokens.
        assert(token.value == ')')
    else
        error(('Unexpected token %q'):format(token.value), 0)
    end

    while lexer:peek() ~= nil do
        local op = lexer:peek()
        if op.value == ')' then
            break
        end
        if op.type ~= 'operation' then
            error(('Expected an operation, got %q'):format(op.value), 0)
        end
        local left_bpower = left_bpower_map[op.value]
        if left_bpower < min_bpower then
            break
        end

        lexer:next()
        local right_bpower = right_bpower_map[op.value]
        local right = parse_expr(lexer, right_bpower)

        left = {
            type = 'operation',
            value = op.value,
            left = left,
            right = right,
        }
    end

    return left
end

local function parse(s)
    if type(s) ~= 'string' then
        error(('Expected a string as an expression, got %s'):format(type(s)), 0)
    end

    -- A convenience wrapper for the lexer.
    local lexer = setmetatable({
        tokens = expression_lexer.split(s),
        pos = 1,
    }, {
        __index = {
            peek = function(self)
                return self.tokens[self.pos]
            end,
            next = function(self)
                self.pos = self.pos + 1
                return self.tokens[self.pos - 1]
            end,
        },
    })

    local ast = parse_expr(lexer, 0)
    if lexer:peek() ~= nil then
        error(('Expected end of an expression, got %q'):format(
            lexer:peek().value), 0)
    end
    return ast
end

return {
    parse = parse,
}
