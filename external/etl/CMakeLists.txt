# ./external/etl/CMakeLists.txt
# CMake configuration for the ETL (Embedded Template Library) header-only library.

# Register this directory as an ESP-IDF component named 'etl'
idf_component_register()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    # Shared source directory path
    set(SHARED_ETL_DIR "${CMAKE_CURRENT_LIST_DIR}/etl-src")

    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        if(EXISTS "${SHARED_ETL_DIR}/include/etl/version.h")
            set(etl_SOURCE_DIR "${SHARED_ETL_DIR}" CACHE INTERNAL "")
            set(etl_POPULATED TRUE CACHE INTERNAL "")
            message(STATUS "ETL | Using shared source directory: ${SHARED_ETL_DIR}")
        else()
            message(STATUS "ETL | Shared source not found or incomplete. Downloading...")
            FetchContent_Populate(
                etl
                URL                         https://github.com/ETLCPP/etl/archive/refs/tags/20.39.4.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP  TRUE
                SOURCE_DIR                  "${SHARED_ETL_DIR}"
            )

            # Remove non-essential directories
            set(DIRS_TO_REMOVE "test" "examples" "doc" "images" "uml" ".git" "arduino" "scripts" "subprojects" "support")
            foreach(dir ${DIRS_TO_REMOVE})
                if(EXISTS "${etl_SOURCE_DIR}/${dir}")
                    file(REMOVE_RECURSE "${etl_SOURCE_DIR}/${dir}")
                endif()
            endforeach()
        endif()
    endif()

    # ETL is header-only
    target_include_directories(${COMPONENT_LIB} INTERFACE "${SHARED_ETL_DIR}/include")
endif()
