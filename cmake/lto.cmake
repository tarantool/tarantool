##
## Manage LTO (Link-Time-Optimization) and IPO (Inter-Procedural-Optimization)
##

if(NOT CMAKE_VERSION VERSION_LESS 3.9)
  cmake_policy(SET CMP0069 NEW)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT CMAKE_INTERPROCEDURAL_OPTIMIZATION_AVAILABLE)
else()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_AVAILABLE FALSE)
endif()

if(NOT CMAKE_BUILD_TYPE_UPPERCASE)
  string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPERCASE)
endif()

# just reset before detection
set(CMAKE_LTO_AVAILABLE FALSE)

#
# Check for LTO support by GCC
if(CMAKE_COMPILER_IS_GNUCC)
  unset(gcc_collect)
  unset(gcc_lto_wrapper)

  if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -v
      OUTPUT_VARIABLE gcc_info_v ERROR_VARIABLE gcc_info_v)

    string(REGEX MATCH "^(.+\nCOLLECT_GCC=)([^ \n]+)(\n.+)$" gcc_collect_valid ${gcc_info_v})
    if(gcc_collect_valid)
      string(REGEX REPLACE "^(.+\nCOLLECT_GCC=)([^ \n]+)(\n.+)$" "\\2" gcc_collect ${gcc_info_v})
    endif()

    string(REGEX MATCH "^(.+\nCOLLECT_LTO_WRAPPER=)([^ \n]+/lto-wrapper)(\n.+)$" gcc_lto_wrapper_valid ${gcc_info_v})
    if(gcc_lto_wrapper_valid)
      string(REGEX REPLACE "^(.+\nCOLLECT_LTO_WRAPPER=)([^ \n]+/lto-wrapper)(\n.+)$" "\\2" gcc_lto_wrapper ${gcc_info_v})
    endif()

    set(gcc_suffix "")
    if(gcc_collect_valid AND gcc_collect)
      string(REGEX MATCH "^(.*cc)(-.+)$" gcc_suffix_valid ${gcc_collect})
      if(gcc_suffix_valid)
        string(REGEX MATCH "^(.*cc)(-.+)$" "\\2" gcc_suffix ${gcc_collect})
      endif()
    endif()

    get_filename_component(gcc_dir ${CMAKE_C_COMPILER} DIRECTORY)
    if(NOT CMAKE_GCC_AR)
      find_program(CMAKE_GCC_AR NAMES gcc${gcc_suffix}-ar gcc-ar${gcc_suffix} PATHS ${gcc_dir} NO_DEFAULT_PATH)
    endif()
    if(NOT CMAKE_GCC_NM)
      find_program(CMAKE_GCC_NM NAMES gcc${gcc_suffix}-nm gcc-nm${gcc_suffix} PATHS ${gcc_dir} NO_DEFAULT_PATH)
    endif()
    if(NOT CMAKE_GCC_RANLIB)
      find_program(CMAKE_GCC_RANLIB NAMES gcc${gcc_suffix}-ranlib gcc-ranlib${gcc_suffix} PATHS ${gcc_dir} NO_DEFAULT_PATH)
    endif()

    unset(gcc_dir)
    unset(gcc_suffix_valid)
    unset(gcc_suffix)
    unset(gcc_lto_wrapper_valid)
    unset(gcc_collect_valid)
    unset(gcc_collect)
    unset(gcc_info_v)
  endif()

  if(CMAKE_GCC_AR AND CMAKE_GCC_NM AND CMAKE_GCC_RANLIB AND gcc_lto_wrapper)
    message(STATUS "Found GCC's LTO toolset: ${gcc_lto_wrapper}, ${CMAKE_GCC_AR}, ${CMAKE_GCC_RANLIB}")
    set(GCC_LTO_CFLAGS "-flto -fno-fat-lto-objects -fuse-linker-plugin")
    set(CMAKE_LTO_AVAILABLE TRUE)
    message(STATUS "Link-Time Optimization by GCC is available")
  else()
    message(STATUS "Link-Time Optimization by GCC is NOT available")
  endif()
  unset(gcc_lto_wrapper)
endif()

#
# Check for LTO support by CLANG
if(CMAKE_COMPILER_IS_CLANG)
  if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.5)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-search-dirs
      OUTPUT_VARIABLE clang_search_dirs)

    unset(clang_bindir)
    unset(clang_libdir)
    string(REGEX MATCH "^(.*programs: =)([^:]*:)*([^:]+/llvm[-.0-9]+/bin[^:]*)(:[^:]*)*(\n.+)$" clang_bindir_valid ${clang_search_dirs})
    if(clang_bindir_valid)
      string(REGEX REPLACE "^(.*programs: =)([^:]*:)*([^:]+/llvm[-.0-9]+/bin[^:]*)(:[^:]*)*(\n.+)$" "\\3" clang_bindir ${clang_search_dirs})
      get_filename_component(clang_libdir "${clang_bindir}/../lib" REALPATH)
      if(clang_libdir)
        message(STATUS "Found CLANG/LLVM directories: ${clang_bindir}, ${clang_libdir}")
      endif()
    endif()

    if(NOT (clang_bindir AND clang_libdir))
      message(STATUS "Could NOT find CLANG/LLVM directories (bin and/or lib).")
    endif()

    if(NOT CMAKE_CLANG_LD AND clang_bindir)
      find_program(CMAKE_CLANG_LD NAMES llvm-link link llvm-ld ld PATHS ${clang_bindir} NO_DEFAULT_PATH)
    endif()
    if(NOT CMAKE_CLANG_AR AND clang_bindir)
      find_program(CMAKE_CLANG_AR NAMES llvm-ar ar PATHS ${clang_bindir} NO_DEFAULT_PATH)
    endif()
    if(NOT CMAKE_CLANG_NM AND clang_bindir)
      find_program(CMAKE_CLANG_NM NAMES llvm-nm nm PATHS ${clang_bindir} NO_DEFAULT_PATH)
    endif()
    if(NOT CMAKE_CLANG_RANLIB AND clang_bindir)
      find_program(CMAKE_CLANG_RANLIB NAMES llvm-ranlib ranlib PATHS ${clang_bindir} NO_DEFAULT_PATH)
    endif()

    set(clang_lto_plugin_name "LLVMgold${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(NOT CMAKE_LD_GOLD AND clang_bindir)
      find_program(CMAKE_LD_GOLD NAMES ld.gold PATHS)
    endif()
    if(NOT CLANG_LTO_PLUGIN AND clang_libdir)
      find_file(CLANG_LTO_PLUGIN ${clang_lto_plugin_name} PATH ${clang_libdir} NO_DEFAULT_PATH)
    endif()
    if(CLANG_LTO_PLUGIN)
      message(STATUS "Found CLANG/LLVM's plugin for LTO: ${CLANG_LTO_PLUGIN}")
    else()
      message(STATUS "Could NOT find CLANG/LLVM's plugin (${clang_lto_plugin_name}) for LTO.")
    endif()

    if(CMAKE_CLANG_LD AND CMAKE_CLANG_AR AND CMAKE_CLANG_NM AND CMAKE_CLANG_RANLIB)
      message(STATUS "Found CLANG/LLVM's binutils for LTO: ${CMAKE_CLANG_AR}, ${CMAKE_CLANG_RANLIB}")
    else()
      message(STATUS "Could NOT find CLANG/LLVM's binutils (ar, ranlib, nm) for LTO.")
    endif()

    unset(clang_lto_plugin_name)
    unset(clang_libdir)
    unset(clang_bindir_valid)
    unset(clang_bindir)
    unset(clang_search_dirs)
  endif()

  if((CLANG_LTO_PLUGIN AND CMAKE_LD_GOLD) AND
      (CMAKE_CLANG_LD AND CMAKE_CLANG_AR AND CMAKE_CLANG_NM AND CMAKE_CLANG_RANLIB))
    set(CMAKE_LTO_AVAILABLE TRUE)
    message(STATUS "Link-Time Optimization by CLANG/LLVM is available")
  else()
    message(STATUS "Link-Time Optimization by CLANG/LLVM is NOT available")
  endif()
endif()

#
# check for LTO by MSVC
if(MSVC)
  if(NOT MSVC_VERSION LESS 1600)
    set(CMAKE_LTO_AVAILABLE TRUE)
    message(STATUS "Link-Time Optimization by MSVC is available")
  else()
    message(STATUS "Link-Time Optimization by MSVC is NOT available")
  endif()
endif()

macro(enable_lto_or_ipo)
  if(CMAKE_INTERPROCEDURAL_OPTIMIZATION_AVAILABLE) #############################

    message(STATUS "Indulge Interprocedural Optimization by CMake")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

  elseif(CMAKE_COMPILER_IS_GNUCC) ##############################################

    message(STATUS "Indulge Link-Time Optimization by GCC")
    set(CMAKE_AR ${CMAKE_GCC_AR} CACHE PATH "Path to ar program with LTO-plugin" FORCE)
    set(CMAKE_NM ${CMAKE_GCC_NM} CACHE PATH "Path to nm program with LTO-plugin" FORCE)
    set(CMAKE_RANLIB ${CMAKE_GCC_RANLIB} CACHE PATH "Path to ranlib program with LTO-plugin" FORCE)
    add_compile_flags("C;CXX" ${GCC_LTO_CFLAGS})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LTO_CFLAGS} -fwhole-program")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${GCC_LTO_CFLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${GCC_LTO_CFLAGS}")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5)
      # Pass the same optimization flags to the linker
      set(compile_flags "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${CMAKE_BUILD_TYPE_UPPERCASE}}")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${compile_flags}")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${compile_flags}")
      set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${compile_flags}")
      unset(compile_flags)
    else()
      add_compile_flags("CXX" "-flto-odr-type-merging")
    endif()

  elseif(CMAKE_COMPILER_IS_CLANG) ##############################################

    message(STATUS "Indulge Link-Time Optimization by CLANG")
    if(CMAKE_CLANG_AR)
      set(CMAKE_AR ${CMAKE_CLANG_AR} CACHE PATH "Path to ar program with LTO-plugin" FORCE)
    endif()
    if(CMAKE_CLANG_RANLIB)
      set(CMAKE_RANLIB ${CMAKE_CLANG_RANLIB} CACHE PATH "Path to ranlib program with LTO-plugin" FORCE)
    endif()
    if(CMAKE_CLANG_NM)
      set(CMAKE_NM ${CMAKE_CLANG_NM} CACHE PATH "Path to nm program with LTO-plugin" FORCE)
    endif()
    add_compile_flags("C;CXX" "-flto")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto -fwhole-program")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -flto")

  elseif(MSVC) #################################################################

    message(STATUS "Indulge Link-Time Optimization by MSVC")
    add_compile_flags("C;CXX" "/GL")
    foreach(linkmode IN ITEMS EXE SHARED STATIC MODULE)
      set(CMAKE_${linkmode}_LINKER_FLAGS "${CMAKE_${linkmode}_LINKER_FLAGS} /LTCG")
      # Remove /INCREMENTAL to avoid warning message from linker due the /LTCG
      # But nowadays this 'removal' has no effect, because a NDA-bug in the CMake (at least up to 3.9.0-rc5)
      string(REGEX REPLACE "^(.*)(/INCREMENTAL:NO *)(.*)$" "\\1\\3" CMAKE_${linkmode}_LINKER_FLAGS "${CMAKE_${linkmode}_LINKER_FLAGS}")
      string(REGEX REPLACE "^(.*)(/INCREMENTAL:YES *)(.*)$" "\\1\\3" CMAKE_${linkmode}_LINKER_FLAGS "${CMAKE_${linkmode}_LINKER_FLAGS}")
      string(REGEX REPLACE "^(.*)(/INCREMENTAL *)(.*)$" "\\1\\3" CMAKE_${linkmode}_LINKER_FLAGS "${CMAKE_${linkmode}_LINKER_FLAGS}")
      string(STRIP "${CMAKE_${linkmode}_LINKER_FLAGS}" CMAKE_${linkmode}_LINKER_FLAGS)
      foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES ITEMS Release MinSizeRel RelWithDebInfo Debug)
        string(TOUPPER "${config}" config_uppercase)
        if(DEFINED "CMAKE_${linkmode}_LINKER_FLAGS_${config_uppercase}")
          string(REGEX REPLACE "^(.*)(/INCREMENTAL:NO *)(.*)$" "\\1\\3" altered_flags "${CMAKE_${linkmode}_LINKER_FLAGS_${config_uppercase}}")
          string(REGEX REPLACE "^(.*)(/INCREMENTAL:YES *)(.*)$" "\\1\\3" altered_flags "${altered_flags}")
          string(REGEX REPLACE "^(.*)(/INCREMENTAL *)(.*)$" "\\1\\3" altered_flags "${altered_flags}")
          string(STRIP "${altered_flags}" altered_flags)
          if(NOT "${altered_flags}" STREQUAL "${CMAKE_${linkmode}_LINKER_FLAGS_${config_uppercase}}")
            set(CMAKE_${linkmode}_LINKER_FLAGS_${config_uppercase} "${altered_flags}" CACHE STRING "Altered: '/INCREMENTAL' removed for LTO" FORCE)
          endif()
        endif()
      endforeach(config)
    endforeach(linkmode)
    unset(linkmode)

    foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES ITEMS Release MinSizeRel RelWithDebInfo)
      foreach(lang IN ITEMS C CXX)
        string(TOUPPER "${config}" config_uppercase)
        if(DEFINED "CMAKE_${lang}_FLAGS_${config_uppercase}")
          string(REPLACE "/O2" "/Ox" altered_flags "${CMAKE_${lang}_FLAGS_${config_uppercase}}")
          if(NOT "${altered_flags}" STREQUAL "${CMAKE_${lang}_FLAGS_${config_uppercase}}")
            set(CMAKE_${lang}_FLAGS_${config_uppercase} "${altered_flags}" CACHE STRING "Altered: '/O2' replaced by '/Ox' for LTO" FORCE)
          endif()
        endif()
	unset(config_uppercase)
      endforeach(lang)
    endforeach(config)
    unset(altered_flags)
    unset(lang)
    unset(config)

  else() #######################################################################
    message(WARNING "Unable to engage IPO/LTO.")
  endif()
endmacro(enable_lto_or_ipo)
