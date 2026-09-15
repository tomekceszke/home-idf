# Included by the ESP-IDF build into every project that depends on home-idf.

set(HOME_IDF_TOOLS_DIR ${CMAKE_CURRENT_LIST_DIR}/tools CACHE INTERNAL "home-idf tools directory")

# home_idf_embed_gzip(<component lib> <file>...)
# Gzips each file deterministically at build time and embeds it as binary data:
#   web/app.html -> _binary_app_html_gz_start / _binary_app_html_gz_end
function(home_idf_embed_gzip lib)
    idf_build_get_property(python PYTHON)
    foreach(src ${ARGN})
        get_filename_component(name ${src} NAME)
        string(MAKE_C_IDENTIFIER ${name} target_suffix)
        set(gz ${CMAKE_CURRENT_BINARY_DIR}/${name}.gz)
        add_custom_command(OUTPUT ${gz}
                COMMAND ${python} ${HOME_IDF_TOOLS_DIR}/gzip_asset.py ${src} ${gz}
                DEPENDS ${src} ${HOME_IDF_TOOLS_DIR}/gzip_asset.py
                VERBATIM)
        add_custom_target(home_idf_gz_${target_suffix} DEPENDS ${gz})
        add_dependencies(${lib} home_idf_gz_${target_suffix})
        target_add_binary_data(${lib} ${gz} BINARY)
    endforeach()
endfunction()
