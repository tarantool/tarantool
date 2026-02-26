-- Evaluate C style expression.
--
-- The module implements Pratt's expression parsing algorithm.
--
-- The functionality is quite limited, because the module is
-- internal and is used for one specific task: process tarantool
-- version constraints in the declarative configuration module.
-- (`conditional[N].if`).
--
-- Supported value kinds:
--
-- 1. Version literal: `1.2.3` (always exactly three numeric components).
-- 2. Variable:
--    - starts with [A-Za-z_];
--    - continues with [A-Za-z0-9_];
--    - may contain dot-separated segments (e.g. `foo.bar_baz`), where each
--      dot must be followed by [A-Za-z0-9_].
--
-- The operations are the following.
--
-- 1. Logical OR: ||
-- 2. Logical AND: &&
-- 3. Logical NOT: !
-- 4. Compare: >, <, >=, <=, !=, ==
-- 5. Parentheses: (, )

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

local prefix_bpower_map = {
    ['!'] = 7,
}

local function parse_expr(lexer, min_bpower)
    local left

    local token = lexer:next()
    if token == nil then
        error('Unexpected end of an expression', 0)
    elseif token.type == 'operation' and
        prefix_bpower_map[token.value] ~= nil then
        local rbp = prefix_bpower_map[token.value]
        local expr = parse_expr(lexer, rbp)
        left = {
            type = 'unary',
            value = token.value,
            expr = expr,
        }
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
        if left_bpower == nil then
            error(('Unknown operation %q'):format(op.value), 0)
        end
        if left_bpower < min_bpower then
            break
        end

        lexer:next()
        local right_bpower = right_bpower_map[op.value]
        if right_bpower == nil then
            error(('Unknown operation %q'):format(op.value), 0)
        end
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

local logical_binary_operators = {
    ['||'] = true,
    ['&&'] = true,
}

local compare_binary_operators = {
    ['<'] = true,
    ['>'] = true,
    ['<='] = true,
    ['>='] = true,
    ['=='] = true,
    ['!='] = true,
}

local unary_operators = {
    ['!'] = true,
}

local function resolve_var(vars, name)
    if type(vars) ~= 'table' then
        return nil
    end

    local cur = vars
    for part in name:gmatch('[^%.]+') do
        if type(cur) ~= 'table' then
            return nil
        end
        cur = cur[part]
    end
    return cur
end

-- Infer node result type: 'version' or 'boolean'.
-- Also validates variable existence and its runtime type.
local function node_type(node, vars)
    assert(node ~= nil)
    if node.type == 'version_literal' then
        return 'version'
    elseif node.type == 'variable' then
        local var = resolve_var(vars, node.value)
        if type(var) == 'nil' then
            error(('Unknown variable: %q'):format(node.value), 0)
        end
        if type(var) == 'boolean' then
            return 'boolean'
        end
        if type(var) ~= 'string' then
            error(('Variable %q has type %s, expected string'):format(
                node.value, type(var)), 0)
        end
        if var == '' then
            error(('Expected a version in variable %q, got an empty ' ..
                'string'):format(node.value), 0)
        end
        local components = var:split('.')
        for _, v in ipairs(components) do
            if not v:match('^[0-9]+$') then
                error(('Variable %q is not a version string'):format(
                    node.value), 0)
            end
        end
        if #components ~= 3 then
            error(('Expected a three component version in variable %q, got ' ..
                '%d components'):format(node.value, #components), 0)
        end
        return 'version'
    elseif node.type == 'unary' then
        if unary_operators[node.value] then
            return 'boolean'
        end
        error(('Unknown unary operator %q'):format(node.value), 0)
    elseif node.type == 'operation' then
        local op = node.value
        if logical_binary_operators[node.value] or
        compare_binary_operators[node.value] then
            return 'boolean'
        end
        error(('Unknown operation %q'):format(op), 0)
    else
        assert(false)
    end
end

local function validate_expr(node, vars)
    -- luacheck: ignore 542 empty if branch
    if node.type == 'version_literal' then
        -- Nothing to validate.
    elseif node.type == 'variable' then
        node_type(node, vars)
    elseif node.type == 'unary' then
        if not unary_operators[node.value] then
            error(('Unknown unary operator %q'):format(node.value), 0)
        end
        local t = node_type(node.expr, vars)
        if t ~= 'boolean' then
            error('Unary "!" accepts only boolean expressions', 0)
        end
        validate_expr(node.expr, vars)
    elseif node.type == 'operation' then
        validate_expr(node.left, vars)
        validate_expr(node.right, vars)
        local op = node.value
        local lt = node_type(node.left, vars)
        local rt = node_type(node.right, vars)

        if logical_binary_operators[op] then
            if lt ~= 'boolean' or rt ~= 'boolean' then
                error('A logical operator (&& or ||) accepts only boolean ' ..
                    'expressions as arguments', 0)
            end
            return
        end

        if compare_binary_operators[op] then
            if lt ~= 'version' or rt ~= 'version' then
                error('A comparison operator (<, >, <=, >=, !=, ==) ' ..
                    'requires version literals or variables as arguments', 0)
            end
            return
        end

        error(('Unknown operation %q'):format(op), 0)
    else
        assert(false)
    end
end

local function validate(node, vars)
    if node.type ~= 'operation' and node.type ~= 'unary' then
        error(('An expression should be a predicate, got %s'):format(
            node.type), 0)
    end
    validate_expr(node, vars)
end

local function compare_version(a, b)
    local av = a:split('.')
    local bv = b:split('.')
    assert(#av == 3)
    assert(#bv == 3)
    for i = 1, 3 do
        local ac = tonumber(av[i])
        local bc = tonumber(bv[i])
        if ac ~= bc then
            return ac - bc
        end
    end
    return 0
end

local operations = {
    ['||'] = function(a, b)
        return a or b
    end,
    ['&&'] = function(a, b)
        return a and b
    end,
    ['<'] = function(a, b)
        return compare_version(a, b) < 0
    end,
    ['>'] = function(a, b)
        return compare_version(a, b) > 0
    end,
    ['<='] = function(a, b)
        return compare_version(a, b) <= 0
    end,
    ['>='] = function(a, b)
        return compare_version(a, b) >= 0
    end,
    ['=='] = function(a, b)
        return compare_version(a, b) == 0
    end,
    ['!='] = function(a, b)
        return compare_version(a, b) ~= 0
    end,
}

local unary_operations = {
    ['!'] = function (a)
        return not a
    end,
}

local function evaluate(node, vars)
    if node.type == 'version_literal' then
        return node.value
    elseif node.type == 'variable' then
        return resolve_var(vars, node.value)
    elseif node.type == 'unary' then
        local op = unary_operations[node.value]
        local expr = evaluate(node.expr, vars)
        return op(expr)
    elseif node.type == 'operation' then
        local left = evaluate(node.left, vars)
        local op = operations[node.value]
        local right = evaluate(node.right, vars)
        return op(left, right)
    else
        assert(false)
    end
end

local function eval(s, vars)
    local ast = parse(s)
    validate(ast, vars)
    return evaluate(ast, vars)
end

return {
    parse = parse,
    validate = validate,
    evaluate = evaluate,
    eval = eval,
}
