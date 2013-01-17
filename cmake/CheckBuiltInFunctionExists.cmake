include(CheckCSourceCompiles)

macro(check_builtin_function_exists function variable)
    set(CMAKE_REQUIRED_FLAGS "-Wno-unused-value -Wno-error")
    check_c_source_compiles("int main(void) { ${function}; return 0; }"
        ${variable})
endmacro(check_builtin_function_exists)
