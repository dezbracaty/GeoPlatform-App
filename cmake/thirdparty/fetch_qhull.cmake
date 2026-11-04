include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# App geometry consumes Qhull directly. Reuse Orca's pinned dependency recipe
# and canonical cache rather than adding a second Qhull build or libslicer API.
function(gplatform_fetch_qhull)
    set(QHULL_VERSION "8.0.2")
    if(CMAKE_BUILD_TYPE)
        set(QHULL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    else()
        set(QHULL_BUILD_TYPE "Release")
    endif()
    gplatform_resolve_fetch_cache(QHULL qhull "${QHULL_VERSION}" "${QHULL_BUILD_TYPE}")
    set(FETCH_DEPS_FORWARD_ARGS ${FETCH_CACHE_FORWARD_ARGS})
    include("${GPLATFORM_THIRDPARTY_SOURCE_DIR}/OrcaSlicer/deps/Qhull/Qhull.cmake")
    find_package(Qhull ${QHULL_VERSION} CONFIG REQUIRED
        PATHS "${QHULL_INSTALL_DIR}/lib/cmake/Qhull" NO_DEFAULT_PATH)
endfunction()

gplatform_fetch_qhull()
