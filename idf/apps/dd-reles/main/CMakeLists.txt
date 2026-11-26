# app.c = el programa del usuario (dd_setup / dd_loop). El servicio de
# compilación de /developers reemplaza sólo ese archivo.
idf_component_register(SRCS "main.c" "app.c" INCLUDE_DIRS "."
                       REQUIRES dd_core dd_app
                       # ⚠ WHOLE_ARCHIVE: sin esto el enlazador usa los dd_setup/dd_loop
                       # vacíos (weak) de dd_app y descarta los de app.c.
                       WHOLE_ARCHIVE)
