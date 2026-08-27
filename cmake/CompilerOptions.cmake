function(deeprun_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor
            /EHsc
            /MP
        )
    endif()
endfunction()
