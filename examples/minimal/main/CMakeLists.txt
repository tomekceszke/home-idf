idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES nvs_flash)

home_idf_login_page(${COMPONENT_LIB} NAME "hi-minimal" ACCENT "#56c2e6" ICONS OFF)
home_idf_embed_gzip(${COMPONENT_LIB} ${project_dir}/web/app.html)
