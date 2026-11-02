# OpenGL 4.5 core loader, generated with glad2:
#   glad --api gl:core=4.5 --extensions="" --out-path glad --reproducible c
add_library(glad STATIC src/gl.c)
target_include_directories(glad SYSTEM PUBLIC include)
