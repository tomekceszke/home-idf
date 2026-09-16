# Included by the ESP-IDF build into every project that depends on home-idf.

set(HOME_IDF_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "home-idf directory")
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

# home_idf_login_page(<component lib> NAME <device name> [ACCENT <#rrggbb>] [ICONS ON|OFF])
# Renders the shared sign-in page (web/login.html) with this device's name and accent colour and embeds it
# the same way home_idf_embed_gzip() would, as _binary_login_html_gz_start / _binary_login_html_gz_end.
# The wordmark splits the name on its first hyphen: "w-controller" -> accent "W-" over "controller".
# ICONS OFF drops the manifest and touch-icon links for apps that do not serve them.
function(home_idf_login_page lib)
    cmake_parse_arguments(ARG "" "NAME;ACCENT;ICONS" "" ${ARGN})
    if(NOT ARG_NAME)
        message(FATAL_ERROR "home_idf_login_page: NAME is required")
    endif()
    if(NOT ARG_ACCENT)
        set(ARG_ACCENT "#56c2e6")
    endif()
    if(NOT DEFINED ARG_ICONS)
        set(ARG_ICONS ON)
    endif()

    set(HI_LOGIN_NAME "${ARG_NAME}")
    set(HI_LOGIN_ACCENT "${ARG_ACCENT}")
    string(FIND "${ARG_NAME}" "-" hyphen)
    if(hyphen EQUAL -1)
        string(SUBSTRING "${ARG_NAME}" 0 1 HI_LOGIN_PREFIX)
        string(SUBSTRING "${ARG_NAME}" 1 -1 HI_LOGIN_REST)
        set(HI_LOGIN_HYPHEN "")
    else()
        string(SUBSTRING "${ARG_NAME}" 0 ${hyphen} HI_LOGIN_PREFIX)
        math(EXPR after_hyphen "${hyphen} + 1")
        string(SUBSTRING "${ARG_NAME}" ${after_hyphen} -1 HI_LOGIN_REST)
        set(HI_LOGIN_HYPHEN "-")
    endif()
    if(ARG_ICONS)
        string(CONCAT HI_LOGIN_ICON_LINKS
                "<link rel=\"icon\" href=\"/apple-touch-icon.png\">\n"
                "<link rel=\"apple-touch-icon\" href=\"/apple-touch-icon.png\">\n"
                "<link rel=\"manifest\" href=\"/manifest.webmanifest\">")
    else()
        set(HI_LOGIN_ICON_LINKS "")
    endif()

    set(rendered ${CMAKE_CURRENT_BINARY_DIR}/login_page/login.html)
    configure_file(${HOME_IDF_DIR}/web/login.html ${rendered} @ONLY)
    home_idf_embed_gzip(${lib} ${rendered})
endfunction()

# home_idf_app_page(<component lib> SRC <app.html> NAME <device name>)
# Renders the app page with the shared shell (web/app_shell.css, web/app_shell.js, wordmark, tab bar; see
# tools/render_page.py for the placeholders) and embeds it like home_idf_embed_gzip() would, as
# _binary_app_html_gz_start / _binary_app_html_gz_end when SRC is named app.html.
function(home_idf_app_page lib)
    cmake_parse_arguments(ARG "" "SRC;NAME" "" ${ARGN})
    if(NOT ARG_SRC OR NOT ARG_NAME)
        message(FATAL_ERROR "home_idf_app_page: SRC and NAME are required")
    endif()
    idf_build_get_property(python PYTHON)
    get_filename_component(name ${ARG_SRC} NAME)
    set(rendered ${CMAKE_CURRENT_BINARY_DIR}/app_page/${name})
    add_custom_command(OUTPUT ${rendered}
            COMMAND ${python} ${HOME_IDF_TOOLS_DIR}/render_page.py ${ARG_SRC} ${rendered} --name ${ARG_NAME}
            DEPENDS ${ARG_SRC} ${HOME_IDF_TOOLS_DIR}/render_page.py
                    ${HOME_IDF_DIR}/web/app_shell.css ${HOME_IDF_DIR}/web/app_shell.js
            VERBATIM)
    home_idf_embed_gzip(${lib} ${rendered})
endfunction()
