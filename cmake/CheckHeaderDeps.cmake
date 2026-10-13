# Fails when Ninja recorded no header dependencies for a first-party object file of a build tree.
# Such an object is never recompiled when a header it includes changes, so it can be linked with
# code compiled against another layout of the same structs (memory corruption at run time).
#   cmake -DNINJA=<ninja> -DBUILD_DIR=<build tree> -P CheckHeaderDeps.cmake
execute_process(COMMAND "${NINJA}" -C "${BUILD_DIR}" -t deps OUTPUT_VARIABLE deps RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ninja -t deps failed (${result})")
endif()
string(REGEX MATCHALL "[^\n]+\\.obj: #deps 0," zero "${deps}")
set(bad "")
foreach(line IN LISTS zero)
  string(REGEX REPLACE ": #deps 0,$" "" object "${line}")
  # Third-party code (_deps) and Qt resource blobs (rcc) include no first-party header.
  if(object MATCHES "^(apps|engines|tools|tests)/" AND NOT object MATCHES "/\\.qt/rcc/|/\\.rcc/qrc_")
    list(APPEND bad "${object}")
  endif()
endforeach()
list(LENGTH bad count)
if(count GREATER 0)
  list(JOIN bad "\n  " text)
  message(FATAL_ERROR
    "${count} object file(s) have no recorded header dependencies:\n  ${text}\n"
    "They were compiled from a console whose code page did not match the /showIncludes prefix "
    "(Spanish MSVC + CP850). Rebuild from a UTF-8 console (scripts/dev-env.ps1 sets it) with "
    "'cmake --build --preset <preset> --clean-first'.")
endif()
string(REGEX MATCHALL "\\.obj: #deps" all "${deps}")
list(LENGTH all total)
message(STATUS "header dependencies recorded for every first-party object (${total} objects)")
