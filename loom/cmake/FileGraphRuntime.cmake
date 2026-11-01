if(LOOM_PACKAGE_DOCX_RUNTIME)
  file(GLOB_RECURSE _file_graph_inputs CONFIGURE_DEPENDS
    "${LOOM_ROOT}/tools/file_graph/*.py" "${LOOM_ROOT}/tools/file_graph/*.json"
    "${LOOM_ROOT}/tools/xlsx_basic/*.py" "${LOOM_ROOT}/tools/xlsx_basic/*.json"
    "${LOOM_ROOT}/tools/odt_basic/*.py" "${LOOM_ROOT}/tools/odt_basic/*.json"
    "${LOOM_ROOT}/tools/epub_basic/reader.py" "${LOOM_ROOT}/tools/unknown_structure/contract.py" "${LOOM_ROOT}/tools/unknown_structure/xls.py"
    "${LOOM_ROOT}/tools/video_basic/*.py" "${LOOM_ROOT}/tools/video_basic/*.json"
    "${LOOM_ROOT}/src/import/video_source_plan/policy.json" "${LOOM_ROOT}/src/import/conversation_bundle/*.py" "${LOOM_ROOT}/src/import/conversation_bundle/*.json")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_file_graph_inputs})
  execute_process(COMMAND "${LOOM_DOCX_PYTHON}" -I -B -S "${LOOM_ROOT}/tools/file_graph/stage.py"
    --source "${LOOM_ROOT}/tools" --destination "${CMAKE_BINARY_DIR}/share/loom/file_graph"
    --python "${LOOM_DOCX_PYTHON}" RESULT_VARIABLE _file_graph_result)
  if(NOT _file_graph_result EQUAL 0)
    message(FATAL_ERROR "Cannot stage file graph adapters")
  endif()
  install(DIRECTORY "${CMAKE_BINARY_DIR}/share/loom/file_graph" DESTINATION share/loom COMPONENT runtime)
endif()
