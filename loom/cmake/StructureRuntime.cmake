# Same explicit system interpreter/package component as DOCX; no source fallback.
if(LOOM_PACKAGE_DOCX_RUNTIME)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${LOOM_ROOT}/tools/structure_readers/stage.py"
    "${LOOM_ROOT}/tools/structure_readers/worker.py"
    "${LOOM_ROOT}/tools/structure_readers/profile.json"
    "${LOOM_ROOT}/tools/csv_structure/reader.py"
    "${LOOM_ROOT}/tools/python_ast_basic/reader.py"
    "${LOOM_ROOT}/tools/python_ast_basic/profile.json")
  execute_process(COMMAND "${LOOM_DOCX_PYTHON}" -I -B -S
    "${LOOM_ROOT}/tools/structure_readers/stage.py"
    --source "${LOOM_ROOT}/tools" --destination "${CMAKE_BINARY_DIR}/share/loom/structure_readers"
    --python "${LOOM_DOCX_PYTHON}" RESULT_VARIABLE _structure_result)
  if(NOT _structure_result EQUAL 0)
    message(FATAL_ERROR "Cannot package trusted structure readers")
  endif()
  install(DIRECTORY "${CMAKE_BINARY_DIR}/share/loom/structure_readers"
    DESTINATION share/loom COMPONENT runtime)
endif()
