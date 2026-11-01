# Tools that execute on the build machine while the game targets another OS.
# Callers select ZH_HOST_CXX and, on Apple, ZH_HOST_SDK explicitly.
function(zh_add_host_executable name source)
  set(tool ${CMAKE_CURRENT_BINARY_DIR}/host-tools/${name})
  set(host_flags -std=c++17)
  if(ZH_HOST_SDK)
    list(APPEND host_flags -isysroot ${ZH_HOST_SDK})
  endif()
  # Use a stamp as OUTPUT. CMake/Ninja can omit an OUTPUT rule when that file
  # is also an imported executable's location; the tool is a BYPRODUCT instead.
  add_custom_command(OUTPUT ${tool}.stamp BYPRODUCTS ${tool}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/host-tools
    COMMAND ${ZH_HOST_CXX} ${host_flags} ${source} -o ${tool}
    COMMAND ${CMAKE_COMMAND} -E touch ${tool}.stamp
    DEPENDS ${source} VERBATIM)
  add_custom_target(${name}_host DEPENDS ${tool}.stamp)
  add_executable(${name} IMPORTED GLOBAL)
  set_target_properties(${name} PROPERTIES IMPORTED_LOCATION ${tool})
  add_dependencies(${name} ${name}_host)
endfunction()
