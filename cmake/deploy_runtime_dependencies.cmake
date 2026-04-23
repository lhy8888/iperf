if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

if(NOT DEFINED target_file OR NOT DEFINED target_dir)
    message(FATAL_ERROR "target_file and target_dir must be defined")
endif()

set(_search_dirs)
foreach(_candidate IN ITEMS
        "${qt_core_dll}"
        "${qt_gui_dll}"
        "${qt_widgets_dll}"
        "${qt_network_dll}"
)
    if(_candidate AND EXISTS "${_candidate}")
        file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
        list(APPEND _search_dirs "${_candidate_path}")
    endif()
endforeach()

if(msys2_bin_dir AND EXISTS "${msys2_bin_dir}")
    file(TO_CMAKE_PATH "${msys2_bin_dir}" _msys2_bin_path)
    list(APPEND _search_dirs "${_msys2_bin_path}")
endif()

list(REMOVE_DUPLICATES _search_dirs)

if(NOT _search_dirs)
    message(FATAL_ERROR "No runtime search directories were provided")
endif()

file(TO_CMAKE_PATH "${target_file}" _target_file)
file(TO_CMAKE_PATH "${target_dir}" _target_dir)

set(_runtime_libraries
    "${qt_core_dll}"
    "${qt_gui_dll}"
    "${qt_widgets_dll}"
    "${qt_network_dll}"
)

file(GLOB_RECURSE _plugin_libraries LIST_DIRECTORIES false "${qt_plugins_dir}/*.dll")
if(_plugin_libraries)
    list(APPEND _runtime_libraries ${_plugin_libraries})
endif()

set(_get_runtime_dependencies_args
    EXECUTABLES "${_target_file}"
    DIRECTORIES ${_search_dirs}
    RESOLVED_DEPENDENCIES_VAR _resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR _unresolved_dependencies
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*"
)

list(REMOVE_DUPLICATES _runtime_libraries)
list(APPEND _get_runtime_dependencies_args LIBRARIES ${_runtime_libraries})

file(GET_RUNTIME_DEPENDENCIES ${_get_runtime_dependencies_args})

list(REMOVE_DUPLICATES _resolved_dependencies)

foreach(_resolved_dependency IN LISTS _resolved_dependencies)
    file(TO_CMAKE_PATH "${_resolved_dependency}" _resolved_dependency_path)
    string(TOLOWER "${_resolved_dependency_path}" _resolved_dependency_lower)

    set(_is_packaged_dependency FALSE)
    foreach(_search_dir IN LISTS _search_dirs)
        string(TOLOWER "${_search_dir}" _search_dir_lower)
        string(FIND "${_resolved_dependency_lower}" "${_search_dir_lower}" _search_dir_index)
        if(_search_dir_index EQUAL 0)
            set(_is_packaged_dependency TRUE)
            break()
        endif()
    endforeach()

    if(_is_packaged_dependency)
        get_filename_component(_resolved_dependency_name "${_resolved_dependency_path}" NAME)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_resolved_dependency_path}"
                    "${_target_dir}/${_resolved_dependency_name}"
            RESULT_VARIABLE _copy_result
        )
        if(NOT _copy_result EQUAL 0)
            message(FATAL_ERROR "Failed to copy runtime dependency: ${_resolved_dependency_path}")
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES _unresolved_dependencies)
if(_unresolved_dependencies)
    # Qt's optional plugin DLLs can drag in vendor or system-specific imports that
    # are not required for app startup and are not always available in the build
    # environment. Keep the build green for those known optional dependencies, but
    # still fail on anything unexpected so we do not hide a real packaging miss.
    set(_optional_unresolved_dependencies
        azureattestmanager.dll
        azureattestnormal.dll
        hvsifiletrust.dll
        pdmutilities.dll
        wtdsensor.dll
        fbclient.dll
        libmariadb.dll
        libpq.dll
        wpaxholder.dll
        wtdccm.dll
    )

    set(_ignored_unresolved_dependencies)
    set(_unexpected_unresolved_dependencies)
    foreach(_dependency IN LISTS _unresolved_dependencies)
        string(TOLOWER "${_dependency}" _dependency_lower)
        list(FIND _optional_unresolved_dependencies "${_dependency_lower}" _optional_dependency_index)
        if(_optional_dependency_index GREATER -1)
            list(APPEND _ignored_unresolved_dependencies "${_dependency}")
        else()
            list(APPEND _unexpected_unresolved_dependencies "${_dependency}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES _ignored_unresolved_dependencies)
    list(REMOVE_DUPLICATES _unexpected_unresolved_dependencies)

    if(_ignored_unresolved_dependencies)
        message(WARNING "Ignoring optional runtime dependencies: ${_ignored_unresolved_dependencies}")
    endif()

    if(_unexpected_unresolved_dependencies)
        message(FATAL_ERROR "Unresolved runtime dependencies: ${_unexpected_unresolved_dependencies}")
    endif()
endif()
