# Keep private build-machine paths out of production assertion strings/debug
# metadata. Test executables retain their own fixture lookup paths; their linked
# production libraries still use these stable prefixes.
get_filename_component(PT_PUBLIC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(pt_public_source_prefixes target)
    if(NOT TARGET "${target}" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()
    get_target_property(imported "${target}" IMPORTED)
    if(imported)
        return()
    endif()
    foreach(home_variable HOME USERPROFILE)
        if(DEFINED ENV{${home_variable}} AND NOT "$ENV{${home_variable}}" STREQUAL "")
            target_compile_options("${target}" PRIVATE
                "-ffile-prefix-map=$ENV{${home_variable}}=/build")
        endif()
    endforeach()
    target_compile_options("${target}" PRIVATE
        "-ffile-prefix-map=${PT_PUBLIC_SOURCE_ROOT}=/src/pocket-tracker-extreme")
endfunction()

function(pt_public_library_prefixes directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(kind "${target}" TYPE)
        if(kind MATCHES "^(STATIC|SHARED|OBJECT|MODULE)_LIBRARY$")
            pt_public_source_prefixes("${target}")
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        pt_public_library_prefixes("${child}")
    endforeach()
endfunction()
