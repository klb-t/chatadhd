# Same runtime component/interpreter as the existing DOCX/host reader transport.
if(LOOM_PACKAGE_DOCX_RUNTIME)
  set(_pdf_structure_runtime "${CMAKE_BINARY_DIR}/share/loom/pdf_structure")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${LOOM_ROOT}/src/text/pdf_structure/stage.py"
    "${LOOM_ROOT}/src/text/pdf_structure/projection.py"
    "${LOOM_ROOT}/src/text/pdf_structure/profile.json"
    "${LOOM_ROOT}/src/text/pdf_structure/worker.py")
  execute_process(COMMAND "${LOOM_DOCX_PYTHON}" -I -B -S
    "${LOOM_ROOT}/src/text/pdf_structure/stage.py"
    --source "${LOOM_ROOT}/src/text/pdf_structure"
    --destination "${_pdf_structure_runtime}" --python "${LOOM_DOCX_PYTHON}"
    RESULT_VARIABLE _pdf_structure_result)
  if(NOT _pdf_structure_result EQUAL 0)
    message(FATAL_ERROR "Cannot package trusted PDF projection")
  endif()
  install(DIRECTORY "${_pdf_structure_runtime}" DESTINATION share/loom COMPONENT runtime)
endif()
