# Relocatable helper package. The interpreter is an explicit runtime dependency,
# recorded at configuration; we never install a host Python or resolve PATH at runtime.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
  option(LOOM_PACKAGE_DOCX_RUNTIME "Package the local DOCX reader and interpreter configuration" ON)
else()
  option(LOOM_PACKAGE_DOCX_RUNTIME "Package the local DOCX reader and interpreter configuration" OFF)
endif()
if(LOOM_PACKAGE_DOCX_RUNTIME)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  set(LOOM_DOCX_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH "Trusted absolute DOCX runtime interpreter")
  if(NOT IS_ABSOLUTE "${LOOM_DOCX_PYTHON}")
    message(FATAL_ERROR "LOOM_DOCX_PYTHON must be absolute")
  endif()
  execute_process(COMMAND "${LOOM_DOCX_PYTHON}" -I -B -S -c
    "import sys,zipfile,zlib,pyexpat; assert sys.version_info >= (3,10); print('.'.join(map(str,sys.version_info[:3])))"
    RESULT_VARIABLE _docx_python_result OUTPUT_VARIABLE LOOM_DOCX_PYTHON_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _docx_python_result EQUAL 0)
    message(FATAL_ERROR "DOCX interpreter requires Python >= 3.10 with stdlib zipfile/zlib/pyexpat")
  endif()
  string(REPLACE "\\" "\\\\" LOOM_DOCX_PYTHON_JSON "${LOOM_DOCX_PYTHON}")
  string(REPLACE "\"" "\\\"" LOOM_DOCX_PYTHON_JSON "${LOOM_DOCX_PYTHON_JSON}")
  file(SHA256 "${LOOM_ROOT}/tools/docx_basic/reader.py" LOOM_DOCX_READER_SHA256)
  set(_docx_runtime "${CMAKE_BINARY_DIR}/share/loom/docx_basic")
  file(MAKE_DIRECTORY "${_docx_runtime}")
  configure_file("${LOOM_ROOT}/tools/docx_basic/reader.py" "${_docx_runtime}/reader.py" COPYONLY)
  configure_file("${LOOM_ROOT}/cmake/docx-runtime.json.in" "${_docx_runtime}/runtime.json" @ONLY)
  install(FILES "${_docx_runtime}/reader.py" "${_docx_runtime}/runtime.json"
    DESTINATION share/loom/docx_basic COMPONENT runtime)
endif()
