include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/FetchCacheHelper.cmake")

# These renderer fixtures remain pinned independently of the optional Filament
# binary package. Keep the original upstream revision, hashes and cache paths.
set(GPLATFORM_RENDERER_ENVIRONMENT_VERSION "1.75.0")
gplatform_resolve_fetch_cache(
    GPLATFORM_RENDERER_ENVIRONMENT "filament"
    "${GPLATFORM_RENDERER_ENVIRONMENT_VERSION}" "Release")
set(GPLATFORM_RENDERER_ENVIRONMENT_CACHE_DIR
    "${GPLATFORM_RENDERER_ENVIRONMENT_CACHE_DIR}/assets")
file(MAKE_DIRECTORY "${GPLATFORM_RENDERER_ENVIRONMENT_CACHE_DIR}")

function(_gplatform_fetch_renderer_environment_asset
         OUTPUT_VARIABLE FILE_NAME REPOSITORY_PATH EXPECTED_SHA256)
    set(_destination "${GPLATFORM_RENDERER_ENVIRONMENT_CACHE_DIR}/${FILE_NAME}")
    set(_valid FALSE)
    if(EXISTS "${_destination}")
        file(SHA256 "${_destination}" _actual_sha256)
        if(_actual_sha256 STREQUAL EXPECTED_SHA256)
            set(_valid TRUE)
        else()
            file(REMOVE "${_destination}")
        endif()
    endif()

    if(NOT _valid)
        set(_url
            "https://api.github.com/repos/google/filament/contents/${REPOSITORY_PATH}?ref=v${GPLATFORM_RENDERER_ENVIRONMENT_VERSION}")
        message(STATUS "Renderer environment: downloading ${FILE_NAME}")
        file(DOWNLOAD
            "${_url}"
            "${_destination}"
            EXPECTED_HASH "SHA256=${EXPECTED_SHA256}"
            HTTPHEADER "Accept: application/vnd.github.raw+json"
            HTTPHEADER "User-Agent: GPlatform-CMake"
            STATUS _download_status
            LOG _download_log
            SHOW_PROGRESS)
        list(GET _download_status 0 _status_code)
        list(GET _download_status 1 _status_message)
        if(NOT _status_code EQUAL 0)
            file(REMOVE "${_destination}")
            message(FATAL_ERROR
                "Renderer environment download failed: ${_status_message}\n"
                "${_download_log}")
        endif()
    endif()

    set(${OUTPUT_VARIABLE} "${_destination}" PARENT_SCOPE)
endfunction()

_gplatform_fetch_renderer_environment_asset(
    GPLATFORM_RENDERER_PILLARS_HDR
    "pillars_2k.hdr"
    "third_party/environments/pillars_2k.hdr"
    "e4b5a3e432eab8104c9b8e452357655a672774516c7a36f5beab07f460588be8")
