add_library(cat SHARED cat.cc catsession.cc)
target_link_libraries(cat maxscale-common)
set_target_properties(cat PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(cat experimental)
