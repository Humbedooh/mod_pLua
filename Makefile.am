bin_PROGRAMS = mod_plua.so
instdir = /usr/lib/apache2/modules
mod_plua_so_SOURCES = main.c
mod_plua_so_LDFLAGS = -shared -fPIC @LDFLAGS@ @APR_LIBS@
mod_plua_so_CFLAGS = -fPIC @CFLAGS@ -I/usr/include/apr-1.0
mod_plua_so_LDADD = -lapr-1 -lluajit-5.1

foomoduledir = /usr/lib/apache2/modules
nobase_dist_foomodule_DATA = mod_plua.so
