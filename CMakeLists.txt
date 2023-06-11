cmake_minimum_required(VERSION 3.0)

project(egads)
set(CMAKE_CXX_STANDARD 11)

# system information
set(EGADS_ARCH)
if (APPLE)
	if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "arm64")
		set(EGADS_ARCH "macos-aarch64")
	else()
		set(EGADS_ARCH "macos-x86_64")
	endif()
else()
	if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "aarch64")
		set(EGADS_ARCH "linux-aarch64")
	else()
		set(EGADS_ARCH "linux-x86_64")
	endif()
endif()

# options provided by this library (desired OpenCASCADE version)
set(OCC_VERSION "7.6.0" CACHE STRING "version of OpenCASCADE")

# opencascade variables
string(REPLACE "." "" OCC_VER_RAW ${OCC_VERSION})
set(OCC_EXTRACT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/OpenCASCADE-${OCC_VERSION}-${EGADS_ARCH})
set(OCC_LIB_DIR ${OCC_EXTRACT_DIR}/lib)

# unpack opencascade
set(LIBOPENCASCADE "https://github.com/philipclaude/egads/releases/download/occ")
if (NOT EXISTS ${OCC_EXTRACT_DIR})
	#find_program(gh REQUIRED)
	message(STATUS "downloading OpenCASCADE...")
	set(OCC_URL "${LIBOPENCASCADE}${OCC_VERSION}/${EGADS_ARCH}.tgz")
	message(STATUS ${OCC_URL})
	if (NOT EXISTS opencascade.tgz)
		#file(DOWNLOAD ${OCC_URL} ${CMAKE_BINARY_DIR}/opencascade.tgz SHOW_PROGRESS)
		execute_process(COMMAND gh release download --repo philipclaude/egads occ${OCC_VERSION} --pattern ${EGADS_ARCH}.tgz --output ${CMAKE_BINARY_DIR}/opencascade.tgz)
	endif()
	message(STATUS "extracting OpenCASCADE...")
        execute_process(COMMAND tar xzf ${CMAKE_BINARY_DIR}/opencascade.tgz)	
	execute_process(COMMAND mv ${CMAKE_BINARY_DIR}/OpenCASCADE-${OCC_VERSION} ${OCC_EXTRACT_DIR})
	message(STATUS "done.")
endif()

# target to remove the extracted OpenCASCADE directory, also see the Makefile in extern/
add_custom_target(clean_occ rm -rf ${CMAKE_CURRENT_SOURCE_DIR}/extern/OpenCASCADE-${OCC_VERSION}* ${OCC_EXTRACT_DIR})

# where all the binaries will be created
set(CMAKE_MACOSX_RPATH TRUE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# create the lib directory and link the opencascade libraries
execute_process(COMMAND mkdir -p ${CMAKE_BINARY_DIR}/lib)
execute_process(COMMAND ln -sf ${OCC_LIB_DIR} ${CMAKE_BINARY_DIR}/lib/opencascade)
if (UNIX AND NOT APPLE)
	# https://gitlab.kitware.com/cmake/community/-/wikis/doc/cmake/RPATH-handling
	SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	SET(CMAKE_INSTALL_RPATH "\$ORIGIN/opencascade")
endif()

# find all unpacked OpenCASCADE libraries
set(OCC_LIB_NAMES TKBool TKernel TKFeat TKGeomAlgo TKMath TKOffset TKPrim
    TKTopAlgo TKBRep TKBO TKG2d TKG3d TKGeomBase TKShHealing TKSTEP
    TKSTEP209 TKSTEPBase TKSTEPAttr TKXSBase TKIGES TKFillet)
set(OCC_LIBRARIES)
foreach (occ_lib ${OCC_LIB_NAMES})
    set(lib)
    find_library(lib ${occ_lib} HINTS ${OCC_LIB_DIR} REQUIRED NO_CACHE)
    if (${lib}-NOTFOUND)
        message(FATAL_ERROR "could not find ${occ_lib}")
    endif()
	if (UNIX AND NOT APPLE)
		# if on linux, we also need to append $ORIGIN (location of the binary)
		# to the opencascade libraries so they can find each other
		# this avoids having to set LD_LIBRARY_PATH
		execute_process(COMMAND patchelf --set-rpath \$ORIGIN ${lib})
	endif()
    set(OCC_LIBRARIES ${OCC_LIBRARIES} ${lib})
endforeach()
link_directories(${OCC_LIB_DIR})

# include directories
include_directories(${OCC_EXTRACT_DIR}/include/opencascade)
set(EGADS_OCC_INCLUDE ${OCC_EXTRACT_DIR}/include/opencascade CACHE STRING "path to OpenCASCADE header files")
set(EGADS_OCC_LIBS ${OCC_LIBRARIES} CACHE STRING "OpenCASCADE libraries")

include_directories(include)
include_directories(src)
include_directories(util)
include_directories(util/uvmap)

# add the subdirectories
add_subdirectory(src)
add_subdirectory(lite)
add_subdirectory(examples)
