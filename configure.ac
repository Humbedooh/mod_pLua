AC_INIT([mod_plua], 0.7)
AM_INIT_AUTOMAKE
AC_PROG_CC

my_save_cflags="$CFLAGS"
CFLAGS=-m64
AC_MSG_CHECKING([whether CC supports -m64])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
    [AC_MSG_RESULT([yes])]
    [AM_CFLAGS=-m64],
    [AC_MSG_RESULT([no])]
)
CFLAGS="$my_save_cflags"
AC_SUBST([AM_CFLAGS])

PKG_CHECK_MODULES(LUA, lua5.1 >= 5.1.2)
PKG_CHECK_MODULES(APR, apr-1 >= 1.3)
AC_CHECK_LIB(lua5.1, luaL_newstate)
AC_CHECK_LIB(apr-1, ap_hook_handler)

AC_PROG_RANLIB
AC_CONFIG_FILES(Makefile)
AC_OUTPUT
