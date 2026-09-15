idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES nvs_flash)

home_idf_embed_gzip(${COMPONENT_LIB} ${project_dir}/web/login.html ${project_dir}/web/app.html)
