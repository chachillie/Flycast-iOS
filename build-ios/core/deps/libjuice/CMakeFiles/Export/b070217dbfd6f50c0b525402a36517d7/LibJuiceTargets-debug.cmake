#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LibJuice::LibJuice" for configuration "Debug"
set_property(TARGET LibJuice::LibJuice APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(LibJuice::LibJuice PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libjuice.a"
  )

list(APPEND _cmake_import_check_targets LibJuice::LibJuice )
list(APPEND _cmake_import_check_files_for_LibJuice::LibJuice "${_IMPORT_PREFIX}/lib/libjuice.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
