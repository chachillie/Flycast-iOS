cmake_minimum_required(VERSION 3.16)

project(breakpad_project)

include(ExternalProject)
add_library(breakpad_client STATIC IMPORTED GLOBAL)
if(WIN32)
	ExternalProject_Add(
		breakpad
		SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
		CONFIGURE_COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/configure --build=x86_64-w64-mingw32 --host=x86_64-w64-mingw32 --prefix=${CMAKE_CURRENT_SOURCE_DIR} MAKE=mingw32-make
		PREFIX ${CMAKE_CURRENT_SOURCE_DIR}
		BUILD_COMMAND mingw32-make "CXXFLAGS=-DUNICODE -D_UNICODE -g" AR=/mingw64/bin/ar RC=/mingw64/bin/windres
		BUILD_IN_SOURCE 1
		BUILD_BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/libbreakpad_client.a
	)
	set(breakpad_target_dir ${CMAKE_CURRENT_SOURCE_DIR})
else()
	if(ANDROID)
		set(BREAKPAD_HOST ${CMAKE_SYSTEM_PROCESSOR}-linux-android)
	else()
		set(BREAKPAD_HOST ${CMAKE_SYSTEM_PROCESSOR}-linux-gnu)
	endif()
	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		# assuming c and c++ targets are the same
		set(BREAKPAD_TARGET --target=${CMAKE_CXX_COMPILER_TARGET})
	endif()
	set(BREAKPAD_CONFIG_CMD ${CMAKE_CURRENT_SOURCE_DIR}/configure
		--host=${BREAKPAD_HOST}
		CC=${CMAKE_C_COMPILER}
		"CFLAGS=${CMAKE_C_FLAGS} ${BREAKPAD_TARGET}"
		CXX=${CMAKE_CXX_COMPILER}
		"CXXFLAGS=${CMAKE_CXX_FLAGS} ${BREAKPAD_TARGET}"
		AR=${CMAKE_AR})
	ExternalProject_Add(
		breakpad
		SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
		CONFIGURE_COMMAND ${BREAKPAD_CONFIG_CMD} --prefix=${CMAKE_CURRENT_BINARY_DIR} --disable-processor --disable-tools
		PREFIX ${CMAKE_CURRENT_BINARY_DIR}
		BUILD_COMMAND make src/client/linux/libbreakpad_client.a
		BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/lib/libbreakpad_client.a
	)
	set(breakpad_target_dir ${CMAKE_CURRENT_BINARY_DIR})
endif()
file(MAKE_DIRECTORY ${breakpad_target_dir}/include/breakpad)
set_target_properties(breakpad_client PROPERTIES
	IMPORTED_LOCATION ${breakpad_target_dir}/lib/libbreakpad_client.a
	INTERFACE_INCLUDE_DIRECTORIES "${breakpad_target_dir}/include;${breakpad_target_dir}/include/breakpad")
add_dependencies(breakpad_client breakpad)
