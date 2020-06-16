#!/usr/bin/env tarantool

local fio = require('fio')
local tap = require('tap')
local test = tap.test('require')

test:plan(11)

local subject = require('require')

subject.patch_require()
local registry = subject.get_registry()
test:is(type(registry), 'table', 'require registry is a simple table')
test:is_deeply(registry, {}, 'Freshly patcher require has empty registry')

require('json')

registry = subject.get_registry()
test:is_deeply(registry.json, { value = 1, parents = {} },
        'plain "require" saves stats in the registry')

subject.reset_registry()
registry = subject.get_registry()

test:is_deeply(registry, {}, '"reset_registry" clears it')

local testdir = fio.pathjoin(debug.sourcedir(), './require')
local searchroot = package.searchroot()
package.setsearchroot(testdir)

require('init')

test:is_deeply(registry, {
    ['init'] = { value = 1, parents = {} },

    ['transitive.modA'] = { value = 1, parents = {'init'} },
    ['transitive.modB'] = { value = 1, parents = {'transitive.modA'} },

    ['implicit.modA'] = { value = 1, parents = {'init'} },
    ['implicit.modB'] = { value = 2, parents = {'implicit.modA', 'init'} },
    ['implicit.modC'] = { value = 1, parents = {'implicit.modB'} },

    ['multiple_parents'] = { value = 1, parents = {'init'} },
    ['multiple_parents.modA'] = { value = 2, parents = {'multiple_parents', 'init'} },
    ['multiple_parents.modB'] = { value = 2, parents = {'multiple_parents', 'init'} },
    ['multiple_parents.modC'] = { value = 2, parents = {'multiple_parents', 'init'} },
    ['multiple_parents.modD'] = { value = 2, parents = {'multiple_parents', 'init'} },
    ['multiple_parents.modE'] = {
        value = 2,
        parents = {'multiple_parents.modA', 'multiple_parents.modB'}
    },
    ['multiple_parents.modF'] = {
        value = 2,
        parents = {'multiple_parents.modC', 'multiple_parents.modD'}
    },
    ['multiple_parents.modG'] = {
        value = 2,
        parents = {'multiple_parents.modE', 'multiple_parents.modF'}
    },
}, '"registry" has tree-like structure')

test:is(subject.usage_count(registry, 'transitive.modA'), 1,
        '"modA" required from init script')
test:is(subject.usage_count(registry, 'transitive.modB'), 1,
        '"modB" required from "modA" transitively')

test:is(subject.usage_count(registry, 'implicit.modA'), 1,
        '"modA" required in init script')
test:is(subject.usage_count(registry, 'implicit.modB'), 2,
        '"modB" required both transitively and explicitly')
test:is(subject.usage_count(registry, 'implicit.modC'), 2,
        '"modC" required implicitly second time')

test:is(subject.usage_count(registry, 'multiple_parents.modG'), 8,
        '"modG" considered required through top level requires')

-- Remove loaded modules during the test, reset registry and return searchroot
for mod in pairs(registry) do
    package.loaded[mod] = nil
end

subject.reset_registry()

package.setsearchroot(searchroot)

os.exit(test:check() and 0 or 1)
