cmake_minimum_required(VERSION 3.16)
project(GraphColoring CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GECODE_DIR "C:/Program Files/Gecode")
set(GECODE_INCLUDE "${GECODE_DIR}/include")
set(GECODE_LIB_DIR "${GECODE_DIR}/lib")
set(GECODE_BIN_DIR "${GECODE_DIR}/bin")

set(GUROBI_DIR "C:/gurobi1302/win64")

# Sources
set(COMMON_SOURCES
    src/graph/graph.cpp
    src/heuristic/heuristic.cpp
)

set(MAIN_SOURCES
    ${COMMON_SOURCES}
    src/preprocessing/clique_processing.cpp
    src/cp_coloring/cp.cpp
)

set(MASTER_SOURCES
    ${COMMON_SOURCES}
    src/master/master.cpp
)

# Executables
add_executable(main
    src/main.cpp
    ${MAIN_SOURCES}
)

add_executable(main_master
    src/main_master.cpp
    ${MASTER_SOURCES}
)

# Include dirs
target_include_directories(main PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GECODE_INCLUDE}
)

target_include_directories(main_master PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    "${GUROBI_DIR}/include"
)

# Link libraries
set(GUROBI_LIBS
    "${GUROBI_DIR}/lib/$<IF:$<CONFIG:Debug>,gurobi_c++mdd2017.lib,gurobi_c++md2017.lib>"
    "${GUROBI_DIR}/lib/gurobi130.lib"
)

set(GECODE_LIBS
    "${GECODE_LIB_DIR}/$<IF:$<CONFIG:Debug>,GecodeSupport-6-2-0-d-x64.lib,GecodeSupport-6-2-0-r-x64.lib>"
    "${GECODE_LIB_DIR}/$<IF:$<CONFIG:Debug>,GecodeKernel-6-2-0-d-x64.lib,GecodeKernel-6-2-0-r-x64.lib>"
    "${GECODE_LIB_DIR}/$<IF:$<CONFIG:Debug>,GecodeInt-6-2-0-d-x64.lib,GecodeInt-6-2-0-r-x64.lib>"
    "${GECODE_LIB_DIR}/$<IF:$<CONFIG:Debug>,GecodeSearch-6-2-0-d-x64.lib,GecodeSearch-6-2-0-r-x64.lib>"
)

target_link_libraries(main PRIVATE
    ${GECODE_LIBS}
)

target_link_libraries(main_master PRIVATE
    ${GUROBI_LIBS}
)

# Copy Gecode DLLs for main only
set(GECODE_DLLS
    "${GECODE_BIN_DIR}/GecodeSupport-6-2-0-r-x64.dll"
    "${GECODE_BIN_DIR}/GecodeSupport-6-2-0-d-x64.dll"
    "${GECODE_BIN_DIR}/GecodeKernel-6-2-0-r-x64.dll"
    "${GECODE_BIN_DIR}/GecodeKernel-6-2-0-d-x64.dll"
    "${GECODE_BIN_DIR}/GecodeInt-6-2-0-r-x64.dll"
    "${GECODE_BIN_DIR}/GecodeInt-6-2-0-d-x64.dll"
    "${GECODE_BIN_DIR}/GecodeSearch-6-2-0-r-x64.dll"
    "${GECODE_BIN_DIR}/GecodeSearch-6-2-0-d-x64.dll"
)

foreach(DLL IN LISTS GECODE_DLLS)
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:main>
    )
endforeach()
