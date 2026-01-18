#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LibJuice::LibJuice" for configuration "MinSizeRel"
set_property(TARGET LibJuice::LibJuice APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(LibJuice::LibJuice PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libjuice.a"
  )

list(APPEND _cmake_import_check_targets LibJuice::LibJuice )
list(APPEND _cmake_import_check_files_for_LibJuice::LibJuice "${_IMPORT_PREFIX}/lib/libjuice.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
