bin_PROGRAMS = mod_plua.so
instdir = /usr/lib/apache2/modules
mod_plua_so_SOURCES = main.c
mod_plua_so_LDFLAGS = -fPIC @LDFLAGS@ @LUA_LIBS@
mod_plua_so_CFLAGS = -fPIC @CFLAGS@

foomoduledir = /usr/lib/apache2/modules
nobase_dist_foomodule_DATA = mod_plua.so
