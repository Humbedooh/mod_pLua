/* File: main.c Author: Humbedooh Created on 4. january 2012, 23:30 */

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua definitions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#define LINUX   2
#define _REENTRANT
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#define LUA_COMPAT_MODULE   1
#define PLUA_VERSION        18
#define DEFAULT_ENCTYPE     "application/x-www-form-urlencoded"
#define MULTIPART_ENCTYPE   "multipart/form-data"
#define MAX_VARS            750
#define MAX_MULTIPLES       25
#define PLUA_DEBUG          0
#ifdef _WIN32
#   define sleep(a)    Sleep(a * 1000)
#endif

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua includes
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#   include <httpd.h>
#   include <http_protocol.h>
#   include <http_config.h>
#   include <apr_dbd.h>
#else
#   include <apr-1.0/apr_dbd.h>
#   include <apache2/httpd.h>
#   include <apache2/http_protocol.h>
#   include <apache2/http_config.h>
#   include <unistd.h>
#endif
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <pthread.h>
#include <time.h>
#ifndef R_OK
#   define R_OK    0x04 /* test for read permission */
#endif

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua structs and globals
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

static int  LUA_STATES = 50;    /* Keep 50 states open */
static int  LUA_RUNS = 500;     /* Restart a state after 500 sessions */
static int  LUA_FILES = 200;    /* Number of files to keep cached */
typedef struct
{
    char    filename[257];
    time_t  modified;
    int     refindex;
} plua_files;
typedef struct
{
    int             working;
    int             sessions;
    request_rec     *r;
    lua_State       *state;
    int             typeSet;
    int             returnCode;
    int             youngest;
    struct timespec t;
    plua_files      *files;
} lua_thread;
typedef struct
{
    lua_thread      *states;
    pthread_mutex_t mutex;
} lua_states;
typedef struct
{
    const char  *key;
    int         size;
    int         sizes[MAX_MULTIPLES];
    const char  *values[MAX_MULTIPLES];
} formdata;
static lua_states   Lua_states;

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua compiler and parser functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void plua_print_error(lua_thread *thread, const char *type) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    char        *errX;
    const char  *err = lua_tostring(thread->state, -1);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    err = err ? err : "(nil)";
    errX = ap_escape_html(thread->r->pool, err);
    ap_set_content_type(thread->r, "text/html;charset=ascii");
    ap_rprintf(thread->r, "<h2>%s:</h2><pre>%s</pre>", type, errX ? errX : err);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int module_lua_panic(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    const char  *el;
    lua_thread  *thread = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        el = lua_tostring(L, 1);
        ap_set_content_type(thread->r, "text/html;charset=ascii");
        ap_rprintf(thread->r, "Lua PANIC: %s\r\n", el);
    } else {
        el = lua_tostring(L, 1);
        printf("Lua PANIC: %s\n", el);
    }

    lua_settop(L, 0);
    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void lua_add_code(char **buffer, const char *string) {

    /*~~~~~~~~~~~~~~~~~*/
    char    *b = *buffer;
    /*~~~~~~~~~~~~~~~~~*/

    if (!string) return;
    if (!b) {
        b = (char *) calloc(1, strlen(string) + 1);
        strcpy(b, string);
    } else {

        /*~~~~~~~~~~~*/
        size_t  at = 0;
        /*~~~~~~~~~~~*/

        at = strlen(b);
        b = (char *) realloc(b, at + strlen(string) + 2);
        b[at + strlen(string) + 1] = 0;
        strcpy((char *) (b + at), string);
    }

    *buffer = b;
    return;
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
int lua_parse_file(lua_thread *thread, char *input) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    int     rc = 0;
    char    *output = 0;
    char    *matchStart,
            *matchEnd;
    size_t  at = 0;
    size_t  inputSize = strlen(input);
    char    X = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    while (at < inputSize) {
        matchStart = strstr((char *) input + at, "<?");
        if (matchStart) {

            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            char    *test = (char *) calloc(1, matchStart - input + at + 20);
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            /* Add any preceding raw html as an echo */
            X = matchStart[0];
            matchStart[0] = 0;
#ifdef _WIN32
            _snprintf_c(test, matchStart - input + 14, "echo([=[%s]=]);", input + at);
#else
            snprintf(test, matchStart - input + 14, "echo([=[%s]=]);", input + at);
#endif
            matchStart[0] = X;

            /*
             * ap_rprintf(thread->r, "Adding raw data: <pre>%s</pre><br/>", ap_escape_html(thread->r->pool, test));
             */
            lua_add_code(&output, test);
            free(test);

            /* Find the beginning and end of the plua chunk */
            at = (matchStart - input) + 2;
            matchEnd = strstr((char *) matchStart + 2, "?>");
            if (!matchEnd) matchEnd = (input + strlen(input));
            if (matchEnd) {

                /*~~~~~~~~~~~~~~~*/
                char    *test = 0;
                char    *etest = 0;
                /*~~~~~~~~~~~~~~~*/

                /* <?=variable?> check */
                if (matchStart[2] == '=') {
                    test = (char *) calloc(1, matchEnd - matchStart + 2);
                    etest = (char *) calloc(1, matchEnd - matchStart + 10);
                    strncpy(test, matchStart + 3, matchEnd - matchStart - 3);
                    test[matchEnd - matchStart - 3] = 0;
                    at = matchEnd - input + 2;
                    sprintf(etest, "echo(%s);", test);

                    /*
                     * ap_rprintf(thread->r, "Adding echo-code: <pre>echo(%s)</pre><br/>",
                     * ap_escape_html(thread->r->pool, test));
                     */
                    lua_add_code(&output, etest);
                    free(test);
                    free(etest);
                } /* <? code ?> check */ else {
                    test = (char *) calloc(1, matchEnd - matchStart + 2);
                    strncpy(test, matchStart + 2, matchEnd - matchStart - 2);
                    test[matchEnd - matchStart - 2] = 0;
                    at = matchEnd - input + 2;

                    /*
                     * ap_rprintf(thread->r, "Adding code: <pre>%s</pre><br/>", ap_escape_html(thread->r->pool, test));
                     */
                    lua_add_code(&output, test);
                    free(test);
                }
            }
        } else {
            if (inputSize > at) {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                char    *test = (char *) calloc(1, strlen(input) - at + 20);
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#ifdef _WIN32
                _snprintf(test, strlen(input) - at + 14, "echo([=[%s]=]);", input + at);
#else
                snprintf(test, strlen(input) - at + 14, "echo([=[%s]=]);", input + at);
#endif
                at = inputSize;
                lua_add_code(&output, test);
                free(test);
            }
        };
    }

    rc = luaL_loadstring(thread->state, output ? output : "echo('no input speficied');");
    if (output) free(output);
    return (rc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
int lua_compile_file(lua_thread *thread, const char *filename) {

    /*~~~~~~~~~~~~~~~~~~~*/
    FILE        *input = 0;
    char        *iBuffer;
    size_t      iSize;
    int         rc = 0;
    int         x = 0,
                y = 0;
    int         found = 0;
    struct stat statbuf;
    /*~~~~~~~~~~~~~~~~~~~*/

    stat(filename, &statbuf);
    for (x = 0; x < LUA_FILES; x++) {
        if (PLUA_DEBUG) {
            if (strlen(thread->files[x].filename))
                ap_rprintf(thread->r, "Checking: %s <=> %s ?<br/>", thread->files[x].filename, filename);
        }

        if (!strcmp(thread->files[x].filename, filename)) {
            if (statbuf.st_mtime != thread->files[x].modified) {
                if (PLUA_DEBUG) ap_rprintf(thread->r, "Deleted out-of-date compiled version at index %u", x);
                memset(thread->files[x].filename, 0, 256);
                luaL_unref(thread->state, LUA_REGISTRYINDEX, thread->files[x].refindex);
                break;
            } else {
                if (PLUA_DEBUG) ap_rprintf(thread->r, "Found usable compiled version at index %u", x);
                found = thread->files[x].refindex;
                return (found);
            }
        }
    }

    if (!found) {
        input = fopen(filename, "r");
        if (input) {
            fseek(input, 0, SEEK_END);
            iSize = ftell(input);
            fseek(input, 0, SEEK_SET);
            iSize = iSize ? iSize : 1;
            iBuffer = (char *) calloc(1, iSize + 1);
            iSize = fread(iBuffer, iSize, 1, input);
            fclose(input);
            rc = lua_parse_file(thread, iBuffer);
            free(iBuffer);
            if (rc == LUA_ERRSYNTAX) {
                return (-2);
            }

            if (!rc) {

                /*~~~~~~~~~~~~~~*/
                int foundSlot = 0;
                /*~~~~~~~~~~~~~~*/

                x = luaL_ref(thread->state, LUA_REGISTRYINDEX);
                if (PLUA_DEBUG) ap_rprintf(thread->r, "Pushed the string from %s onto the registry at index %u<br/>", filename, x);
                for (y = 0; y < LUA_FILES; y++) {
                    if (!strlen(thread->files[y].filename)) {
                        strcpy(thread->files[y].filename, filename);
                        thread->files[y].modified = statbuf.st_mtime;
                        thread->files[y].refindex = x;
                        foundSlot = 1;
                        thread->youngest = y;
                        if (PLUA_DEBUG) ap_rprintf(thread->r, "New file: Pushed it into the file list at index %u<br/>", y);
                        break;
                    }
                }

                if (!foundSlot) {

                    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                    /* All slots are full, use the oldest slot for the new file */
                    int y = (thread->youngest + 1) % LUA_FILES;
                    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                    thread->youngest = y;
                    strcpy(thread->files[y].filename, filename);
                    thread->files[y].modified = statbuf.st_mtime;
                    thread->files[y].refindex = x;
                    if (PLUA_DEBUG) ap_rprintf(thread->r, "Pushed the into the file list at index %u, replacing an old file<br/>", y);
                }

                return (x);
            }
        } else return (-1);
    }

    return (-1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
char *getPWD(lua_thread *thread) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    size_t  x,
            y,
            z;
    char    *pwd = (char *) apr_pcalloc(thread->r->pool, strlen(thread->r->filename) + 1);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (pwd) {
        strcpy(pwd, thread->r->filename);
        y = strlen(pwd);
        z = 0;
        for (x = 0; x < y; x++) {
            if (pwd[x] == '/' || pwd[x] == '\\') z = x;
        }

        if (z) pwd[z] = 0;
        return (pwd);
    } else return (0);
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua Lua functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_fileexists(lua_State *L) {

    /*~~~~~~~~~~~~*/
    const char  *el;
    /*~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    el = lua_tostring(L, 1);
    lua_settop(L, 0);
#ifdef _WIN32
    if (access(el, 0) == 0) lua_pushboolean(L, 1);
#else
    if (faccessat(0, el, R_OK, AT_EACCESS) == 0) lua_pushboolean(L, 1);
#endif
    else lua_pushboolean(L, 0);
    return (1);
}

typedef struct
{
    apr_dbd_t               *handle;
    const apr_dbd_driver_t  *driver;
} dbStruct;

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_dbclose(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, 1, 0);
    luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
    db = (dbStruct *) lua_topointer(L, -1);
    if (db) {
        rc = apr_dbd_close(db->driver, db->handle);
    }

    lua_pushnumber(L, rc);
    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_dbdo(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    lua_thread      *thread;
    int             x = 0;
    const char      *statement;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TSTRING);
        statement = lua_tostring(L, 2);
        lua_rawgeti(L, 1, 0);
        luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
        db = (dbStruct *) lua_topointer(L, -1);
        if (db) {
            rc = apr_dbd_query(db->driver, db->handle, &x, statement);
        } else {
            rc = 0;
            x = -1;
        }

        if (rc == APR_SUCCESS) lua_pushnumber(L, x);
        else lua_pushboolean(L, 0);
        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_dbescape(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~*/
    dbStruct    *db = 0;
    lua_thread  *thread;
    const char  *statement;
    const char  *escaped = 0;
    /*~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TSTRING);
        statement = lua_tostring(L, 2);
        lua_rawgeti(L, 1, 0);
        luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
        db = (dbStruct *) lua_topointer(L, -1);
        if (db) {
            escaped = apr_dbd_escape(db->driver, thread->r->pool, statement, db->handle);
            if (escaped) {
                lua_pushstring(L, escaped);
                return (1);
            }
        } else lua_pushnil(L);
        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_dbquery(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    lua_thread      *thread;
    const char      *statement;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TSTRING);
        statement = lua_tostring(L, 2);
        lua_rawgeti(L, 1, 0);
        luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
        db = (dbStruct *) lua_topointer(L, -1);
        if (db) {

            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            int                 rows,
                                cols;
            apr_dbd_results_t   *results = 0;
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            rc = apr_dbd_select(db->driver, thread->r->pool, db->handle, &results, statement, 0);
            if (rc == APR_SUCCESS) {

                /*~~~~~~~~~~~~~~~~~~~~~*/
                const char      *entry;
                apr_dbd_row_t   *row = 0;
                int             x;
                /*~~~~~~~~~~~~~~~~~~~~~*/

                cols = apr_dbd_num_cols(db->driver, results);
                lua_newtable(L);
                if (cols > 0) {
                    for (rows = 1; apr_dbd_get_row(db->driver, thread->r->pool, results, &row, rows) != -1; rows++) {
                        lua_pushinteger(L, rows);
                        lua_newtable(L);
                        for (x = 0; x < cols; x++) {
                            entry = apr_dbd_get_entry(db->driver, row, x);
                            if (entry) {
                                lua_pushinteger(L, x + 1);
                                lua_pushstring(L, entry);
                                lua_rawset(L, -3);
                            }
                        }

                        lua_rawset(L, -3);
                    }

                    return (1);
                }
            }
        }

        lua_pushboolean(L, 0);
        return (1);
    }

    return (0);
}

static const luaL_reg   db_methods[] =
{
    { "escape", lua_dbescape },
    { "close", lua_dbclose },
    { "query", lua_dbquery },
    { "run", lua_dbdo },
    { 0, 0 }
};

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_dbopen(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    const char      *type;
    const char      *arguments;
    lua_thread      *thread;
    dbStruct        *db = (dbStruct *) calloc(1, sizeof(dbStruct));
    apr_status_t    rc = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        luaL_checktype(L, 1, LUA_TSTRING);
        luaL_checktype(L, 2, LUA_TSTRING);
        type = lua_tostring(L, 1);
        arguments = lua_tostring(L, 2);
        lua_settop(L, 0);
        apr_dbd_init(thread->r->pool);
        rc = apr_dbd_get_driver(thread->r->pool, type, &db->driver);
        if (rc == APR_SUCCESS) {
            rc = apr_dbd_open(db->driver, thread->r->pool, arguments, &db->handle);
            if (rc == APR_SUCCESS) {
                lua_newtable(L);
                luaL_register(L, NULL, db_methods);
                lua_pushlightuserdata(L, db);
                lua_rawseti(L, -2, 0);
                return (1);
            } else {
                lua_pushnil(thread->state);
                return (1);
            }

            lua_pushboolean(thread->state, 0);
            return (1);
        } else {
            lua_pushfstring(thread->state, "The database driver for '%s' could not be found!", type);
            plua_print_error(thread, "Database error");
            lua_pushnil(thread->state);
            return (1);
        }
    }

    lua_pushnil(thread->state);
    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_header(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *key,
                *value;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        luaL_checktype(L, 1, LUA_TSTRING);
        luaL_checktype(L, 2, LUA_TSTRING);
        key = lua_tostring(L, 1);
        value = lua_tostring(L, 2);
        lua_settop(L, 0);
        apr_table_set(thread->r->headers_out, key, value);
        if (!strcmp(key, "Location")) thread->returnCode = HTTP_MOVED_TEMPORARILY;
    } else {

        /*
         * ap_rputs("Couldn't find our userdata :(",thread->r);
         */
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_echo(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *el;
    lua_thread  *thread;
    int         y,
                z;
    size_t      x;
    /*~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {

        /*
         * luaL_checktype(L, 1, LUA_TSTRING);
         */
        z = lua_gettop(L);
        for (y = 1; y < z; y++) {
            x = 0;
            el = lua_tolstring(L, y, &x);

            /*
             * if (el) ap_rputs(el, thread->r);
             */
            if (el) ap_rwrite(el, x, thread->r);
        }

        lua_settop(L, 0);
    } else {

        /*
         * ap_rputs("Couldn't find our userdata :(",thread->r);
         */
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_setContentType(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *el;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        luaL_checktype(L, 1, LUA_TSTRING);
        el = lua_tostring(L, 1);
        lua_settop(L, 0);
        ap_set_content_type(thread->r, el);
        thread->typeSet = 1;
    } else {

        /*
         * ap_rputs("Couldn't find our userdata :(",thread->r);
         */
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_setReturnCode(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    int         rc;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        rc = luaL_optint(L, 1, 0);
        lua_settop(L, 0);
        thread->returnCode = rc;
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_getEnv(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    lua_thread                  *thread;
    const apr_array_header_t    *fields;
    int                         i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        apr_table_entry_t   *e = 0;
        char                *pwd = getPWD(thread);
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        lua_newtable(thread->state);
        fields = apr_table_elts(thread->r->headers_in);
        e = (apr_table_entry_t *) fields->elts;
        for (i = 0; i < fields->nelts; i++) {
            lua_pushstring(thread->state, e[i].key);
            lua_pushstring(thread->state, e[i].val);
            lua_rawset(L, -3);
        }

        fields = apr_table_elts(thread->r->subprocess_env);
        e = (apr_table_entry_t *) fields->elts;
        for (i = 0; i < fields->nelts; i++) {

            /*
             * ap_rprintf(thread->r, "--%s: %s--<br/>\n", e[i].key, e[i].val);
             */
            lua_pushstring(thread->state, e[i].key);
            lua_pushstring(thread->state, e[i].val);
            lua_rawset(L, -3);
        }

        /* Our own data */
        lua_pushstring(thread->state, "Plua-States");
        lua_pushinteger(thread->state, LUA_STATES);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Plua-Runs");
        lua_pushinteger(thread->state, LUA_RUNS);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Plua-Sessions");
        lua_pushinteger(thread->state, thread->sessions);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Plua-Files");
        lua_pushinteger(thread->state, LUA_FILES);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Plua-Version");
        lua_pushinteger(thread->state, PLUA_VERSION);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Remote-Address");
        lua_pushstring(thread->state, thread->r->connection->remote_ip);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Plua-Handle");
        lua_pushfstring(thread->state, "%p", thread);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Lua-State");
        lua_pushfstring(thread->state, "%p", thread->state);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Request-Method");
        lua_pushstring(thread->state, thread->r->method);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Filename");
        lua_pushstring(thread->state, thread->r->filename);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "URI");
        lua_pushstring(thread->state, thread->r->uri);
        lua_rawset(L, -3);
        if (pwd) {
            lua_pushstring(thread->state, "Working-Directory");
            lua_pushstring(thread->state, pwd);
            lua_rawset(L, -3);
        }

        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_fileinfo(lua_State *L)
{
#if __WIN
#   define open    _open
#endif

    /*~~~~~~~~~~~~~~~~~~*/
    struct stat fileinfo;
    const char  *filename;
    /*~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    filename = lua_tostring(L, 1);
    lua_settop(L, 0);
    if (stat(filename, &fileinfo) == -1) lua_pushnil(L);
    else {
        lua_newtable(L);
        lua_pushliteral(L, "size");
        lua_pushinteger(L, fileinfo.st_size);
        lua_rawset(L, -3);
        lua_pushliteral(L, "created");
        lua_pushinteger(L, fileinfo.st_ctime);
        lua_rawset(L, -3);
        lua_pushliteral(L, "modified");
        lua_pushinteger(L, fileinfo.st_mtime);
        lua_rawset(L, -3);
        lua_pushliteral(L, "accessed");
        lua_pushinteger(L, fileinfo.st_atime);
        lua_rawset(L, -3);
        lua_pushliteral(L, "mode");
        lua_pushinteger(L, fileinfo.st_mode);
        lua_rawset(L, -3);
    }

    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_clock(lua_State *L)
{
    /*~~~~~~~~~~~~~~~~*/
#ifdef _WIN32
    clock_t         f;
    LARGE_INTEGER   moo;
    LARGE_INTEGER   cow;
#else
    struct timespec t;
#endif
    /*~~~~~~~~~~~~~~~~*/

    lua_settop(L, 0);
    lua_newtable(L);
#ifdef _WIN32
    QueryPerformanceCounter(&moo);
    QueryPerformanceFrequency(&cow);
    f = clock();
    lua_pushliteral(L, "seconds");
    lua_pushinteger(L, moo.QuadPart / cow.QuadPart);
    lua_rawset(L, -3);
    lua_pushliteral(L, "nanoseconds");
    lua_pushinteger(L, moo.QuadPart % cow.QuadPart * (1000000000 / cow.QuadPart));
    lua_rawset(L, -3);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
    lua_pushliteral(L, "seconds");
    lua_pushinteger(L, t.tv_sec);
    lua_rawset(L, -3);
    lua_pushliteral(L, "nanoseconds");
    lua_pushinteger(L, t.tv_nsec);
    lua_rawset(L, -3);
#endif
    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_compileTime(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    lua_settop(L, 0);
    if (thread) {
        lua_newtable(L);
        lua_pushliteral(L, "seconds");
        lua_pushinteger(L, thread->t.tv_sec);
        lua_rawset(L, -3);
        lua_pushliteral(L, "nanoseconds");
        lua_pushinteger(L, thread->t.tv_nsec);
        lua_rawset(L, -3);
        return (1);
    } else return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int util_read(request_rec *r, const char **rbuf, apr_off_t *size) {

    /*~~~~~~~~*/
    int rc = OK;
    /*~~~~~~~~*/

    if ((rc = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR))) {
        return (rc);
    }

    if (ap_should_client_block(r)) {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        char        argsbuffer[HUGE_STRING_LEN];
        int         rsize,
                    len_read,
                    rpos = 0;
        apr_off_t   length = r->remaining;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        *rbuf = (const char *) apr_pcalloc(r->pool, length + 1);

        /*
         * ap_rprintf(r, "Getting data of %u bytes\n", length);
         */
        *size = length;

        /*
         * ap_hard_timeout("util_read", r);
         */
        while ((len_read = ap_get_client_block(r, argsbuffer, sizeof(argsbuffer))) > 0) {

            /*
             * ap_reset_timeout(r);
             */
            if ((rpos + len_read) > length) {
                rsize = length - rpos;
            } else {
                rsize = len_read;
            }

            memcpy((char *) *rbuf + rpos, argsbuffer, rsize);
            rpos += rsize;
        }

        /*
         * ap_kill_timeout(r);
         */
    }

    return (rc);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int parse_urlencoded(lua_thread *thread, const char *data) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    int         z,
                i;
    size_t      x,
                y;
    const char  *key,
                *val;
    formdata    *form = apr_pcalloc(thread->r->pool, sizeof(formdata) * MAX_VARS);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    i = 0;
    while (*data && (val = ap_getword(thread->r->pool, &data, '&'))) {
        i++;
        if (i == MAX_VARS) break;
        key = ap_getword(thread->r->pool, &val, '=');
        y = strlen(val);
        for (x = 0; x < y; x++) {
            if (val[x] == '+') ((char *) val)[x] = ' ';
        }

        ap_unescape_url((char *) key);
        ap_unescape_url((char *) val);
        for (z = 0; z < MAX_VARS; z++) {
            if (form[z].key == 0 || !strcmp(form[z].key, key)) {
                form[z].key = key;
                if (form[z].size == MAX_MULTIPLES) continue;
                form[z].values[form[z].size++] = val;
                break;
            }
        }
    }

    for (z = 0; z < MAX_VARS; z++) {
        if (form[z].key) {
            if (form[z].size == 1) {
                lua_pushstring(thread->state, form[z].key);
                lua_pushstring(thread->state, form[z].values[0]);
                lua_rawset(thread->state, -3);
            } else {
                lua_pushstring(thread->state, form[z].key);
                lua_newtable(thread->state);
                for (i = 0; i < form[z].size; i++) {
                    lua_pushinteger(thread->state, i + 1);
                    lua_pushstring(thread->state, form[z].values[i]);
                    lua_rawset(thread->state, -3);
                }

                lua_rawset(thread->state, -3);
            }
        }
    }

    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int parse_multipart(lua_thread *thread, const char *data, const char *multipart, apr_off_t size) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    char        *buffer;
    char        *key,
                *filename;
    char        *start = 0,
                *end = 0,
                *crlf = 0;
    int         i,
                z;
    size_t      vlen = 0;
    size_t      len = 0;
    formdata    *form = (formdata *) apr_pcalloc(thread->r->pool, sizeof(formdata) * MAX_VARS);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    len = strlen(multipart);
    i = 0;
    for (start = strstr((char *) data, multipart); start != start + size; start = end) {
        i++;
        if (i == MAX_VARS) break;
        end = strstr((char *) (start + 1), multipart);
        if (!end) end = start + size;
        crlf = strstr((char *) start, "\r\n\r\n");
        if (!crlf) break;
        key = (char *) apr_pcalloc(thread->r->pool, 256);
        filename = (char *) apr_pcalloc(thread->r->pool, 256);
        buffer = (char *) apr_palloc(thread->r->pool, end - crlf);
        vlen = end - crlf - 8;
        memcpy(buffer, crlf + 4, vlen);
        sscanf(start + len + 2, "Content-Disposition: form-data; name=\"%255[^\"]\"; filename=\"%255[^\"]\"", key, filename);
        if (strlen(key)) {
            for (z = 0; z < MAX_VARS - 1; z++) {
                if (form[z].key == 0 || !strcmp(form[z].key, key)) {
                    form[z].key = key;
                    if (form[z].size == MAX_MULTIPLES) continue;
                    if (strlen(filename)) {
                        form[z].sizes[form[z].size] = strlen(filename);
                        form[z].values[form[z].size++] = filename;
                    }

                    form[z].sizes[form[z].size] = vlen;
                    form[z].values[form[z].size++] = buffer;
                    break;
                }
            }
        }
    }

    for (z = 0; z < MAX_VARS - 1; z++) {
        if (form[z].key) {
            if (form[z].size == 1) {
                lua_pushstring(thread->state, form[z].key);
                lua_pushlstring(thread->state, form[z].values[0], form[z].sizes[0]);
                lua_rawset(thread->state, -3);
            } else {
                lua_pushstring(thread->state, form[z].key);
                lua_newtable(thread->state);
                for (i = 0; i < form[z].size; i++) {
                    lua_pushinteger(thread->state, i + 1);
                    lua_pushlstring(thread->state, form[z].values[i], form[z].sizes[i]);
                    lua_rawset(thread->state, -3);
                }

                lua_rawset(thread->state, -3);
            }
        }
    }

    return (OK);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_parse_post(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    const char  *data,
                *type;
    char        multipart[256];
    lua_thread  *thread = 0;
    apr_off_t   size = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        lua_newtable(thread->state);
        if (thread->r->method_number != M_POST) {
            return (0);
        }

        memset(multipart, 0, 256);
        type = apr_table_get(thread->r->headers_in, "Content-Type");
        if (sscanf(type, "multipart/form-data; boundary=%250c", multipart) == 1) {
            if (util_read(thread->r, &data, &size) != OK) {
                return (0);
            }

            parse_multipart(thread, data, multipart, size);
            return (1);
        }

        if (strcasecmp(type, DEFAULT_ENCTYPE) == 0) {
            if (util_read(thread->r, &data, &size) != OK) {
                return (0);
            }

            parse_urlencoded(thread, data);
            return (1);
        }

        return (0);
    } else return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_parse_get(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    const char  *data;
    lua_thread  *thread = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        lua_newtable(thread->state);
        data = thread->r->args;
        parse_urlencoded(thread, data);
        return (1);
    } else return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_includeFile(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    const char  *filename;
    lua_thread  *thread = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {

        /*~~~~~~~~~~~~~~~~~*/
        struct stat fileinfo;
        int         rc = 0;
        /*~~~~~~~~~~~~~~~~~*/

        luaL_checktype(L, 1, LUA_TSTRING);
        filename = lua_tostring(L, 1);
        lua_settop(L, 0);
        if (stat(filename, &fileinfo) == -1) lua_pushboolean(L, 0);
        else {
            rc = lua_compile_file(thread, filename);
            if (rc > 0) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, rc);
                rc = lua_pcall(L, 0, LUA_MULTRET, 0);
                if (rc) {
                    plua_print_error(thread, "Run-time error");
                    lua_pushboolean(L, 0);
                } else lua_pushboolean(L, 1);
            } else {
                plua_print_error(thread, "Compiler error in included file");
                lua_pushboolean(L, 0);
            }
        }
    }

    return (1);
}

static const luaL_reg   File_methods[] = { { "stat", lua_fileinfo }, { "exists", lua_fileexists }, { 0, 0 } };
static const luaL_reg   Global_methods[] =
{
    { "echo", lua_echo },
    { "print", lua_echo },
    { "header", lua_header },
    { "setContentType", lua_setContentType },
    { "getEnv", lua_getEnv },
    { "parsePost", lua_parse_post },
    { "parseGet", lua_parse_get },
    { "clock", lua_clock },
    { "compileTime", lua_compileTime },
    { "setReturnCode", lua_setReturnCode },
    { "include", lua_includeFile },
    { "dbopen", lua_dbopen },
    { 0, 0 }
};

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void register_lua_functions(lua_State *L) {
    lua_atpanic(L, module_lua_panic);
    luaL_register(L, "file", File_methods);
    luaL_register(L, "_G", Global_methods);
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua state functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
    Creates and initializes a new Lua state @param thread the lua_thread pointer to use @param x The index of the state
    in the grand scheme
 =======================================================================================================================
 */
void lua_init_state(lua_thread *thread, int x) {

    /*~~~~~~~~~~~*/
    lua_State   *L;
    int         y;
    /*~~~~~~~~~~~*/

    thread->youngest = 0;
    thread->state = luaL_newstate();
    thread->sessions = 0;
    L = (lua_State *) thread->state;
    lua_pushinteger(L, x);
    luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_openlibs(L);
    luaopen_debug(L);
    register_lua_functions(L);
    for (y = 0; y < LUA_FILES; y++) {
        memset(thread->files[y].filename, 0, 256);
        thread->files[y].modified = 0;
        thread->files[y].refindex = 0;
    }
}

/*
 =======================================================================================================================
    Acquires a Lua state from the global stack
 =======================================================================================================================
 */
lua_thread *lua_acquire_state(void) {

    /*~~~~~~~~~~~~~~~~~~*/
    int         x;
    int         found = 0;
    lua_thread  *L = 0;
    /*~~~~~~~~~~~~~~~~~~*/

#ifndef _WIN32
    pthread_mutex_lock(&Lua_states.mutex);
#endif
    for (x = 0; x < LUA_STATES; x++) {
        if (!Lua_states.states[x].working && Lua_states.states[x].state) {
            Lua_states.states[x].working = 1;
            Lua_states.states[x].sessions++;
            L = &Lua_states.states[x];
            found = 1;
            break;
        }
    }

#ifndef _WIN32
    pthread_mutex_unlock(&Lua_states.mutex);
#endif
    if (found)
    {
        /*~~~~~~~~~~~~~~~~*/
#ifdef _WIN32
        LARGE_INTEGER   moo;
        LARGE_INTEGER   cow;
        /*~~~~~~~~~~~~~~~~*/

        QueryPerformanceCounter(&moo);
        QueryPerformanceFrequency(&cow);
        L->t.tv_sec = (moo.QuadPart / cow.QuadPart);
        L->t.tv_nsec = (moo.QuadPart % cow.QuadPart) * (1000000000 / cow.QuadPart);
#else
        clock_gettime(CLOCK_MONOTONIC, &L->t);
#endif
        return (L);
    } else {
        sleep(1);
        return (lua_acquire_state());
    }
}

/*
 =======================================================================================================================
    Releases a Lua state and frees it up for use elsewhere @param X the lua_State to release
 =======================================================================================================================
 */
void lua_release_state(lua_State *X) {

    /*~~*/
    int x;
    /*~~*/

    lua_gc(X, LUA_GCSTEP, 1);
#ifndef _WIN32
    pthread_mutex_lock(&Lua_states.mutex);
#endif
    for (x = 0; x < LUA_STATES; x++) {
        if (Lua_states.states[x].state == X) {
            Lua_states.states[x].working = 0;

            /* Check if state needs restarting */
            if (Lua_states.states[x].sessions >= LUA_RUNS) {
                lua_close(Lua_states.states[x].state);
                lua_init_state(&Lua_states.states[x], x);
            }
            break;
        }
    }

#ifndef _WIN32
    pthread_mutex_unlock(&Lua_states.mutex);
#endif
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_plua hooks and handlers
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
 * plua_handler(request_rec* r):
 * The handler for events set to be handled by 'plua' in apache.
 * It parses, compiles, caches and runs the files using all the functions above.
 =======================================================================================================================
 */
static int plua_handler(request_rec *r) {

    /*~~~~~~~~~~~~~~~~~~~*/
    int         exists = 1;
    struct stat statbuf;
    /*~~~~~~~~~~~~~~~~~~~*/

    // Check if we are being called, and that the request method is one we can handle.
    if (!r->handler || strcmp(r->handler, "plua")) return (DECLINED);
    if (r->method_number != M_GET && r->method_number != M_POST) return (HTTP_METHOD_NOT_ALLOWED);
    
    // Check if the file requested really exists.
    if (stat(r->filename, &statbuf) == -1) exists = 0;
    else if (statbuf.st_mode & 0x00400000) // Is it a folder?
        exists = 0;
    
    // If no file (or if it's a folder), print out the 404 message.
    if (!exists) {
        ap_set_content_type(r, "text/html;charset=ascii");
        ap_rputs("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n", r);
        ap_rputs("<html><head><title>Lua_HTML: Error</title></head>", r);
        ap_rprintf(r, "<body><h1>No such file: %s</h1></body></html>", r->unparsed_uri);
        return (HTTP_NOT_FOUND);
        
    // Else start processing the request
    } else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        int         x = 0,
                    rc = 0;
        lua_thread  *l = lua_acquire_state();
        lua_State   *L = l->state;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        // Set up the lua_thread struct and change to the current directory.
        l->r = r;
#ifndef _WIN32
        rc = chdir(getPWD(l));
#else
        SetCurrentDirectoryA(getPWD(l));
#endif
        // Set default return code to OK (200-ish)
        l->returnCode = OK;
        
        // Push the lua_thread struct onto the Lua registry (this should be changed to an init operation?)
        lua_pushlightuserdata(L, l);
        x = luaL_ref(L, LUA_REGISTRYINDEX);
        
        // Call the compiler function and let it either compile or read from cache.
        rc = lua_compile_file(l, r->filename);
        
        // Compiler error?
        if (rc < 1) {
            plua_print_error(l, "Compiler error");
            rc = OK;
        
        // No compiler error, let's run this file.
        } else {
            // Get the compiled binary chunk off the registry.           
            lua_rawgeti(L, LUA_REGISTRYINDEX, rc);
            
            // Set content-type to default.
            l->typeSet = 0;
            rc = 0;
            
            // Run the compiled code.
            rc = lua_pcall(L, 0, LUA_MULTRET, 0);
            
            // DId we get a run-time error?
            if (rc) {
                plua_print_error(l, "Run-time error");
                rc = OK;
            
            // No error, everything went fine, set up the content type if needed and return OK.
            } else {
                if (l->typeSet == 0) ap_set_content_type(r, "text/html;charset=ascii");
                rc = l->returnCode;
                if (PLUA_DEBUG) ap_rprintf(r, "<b>Compiled and ran fine from index %u</b>", rc);
            }
        }

        /* Cleanup */
        luaL_unref(L, LUA_REGISTRYINDEX, x);
        lua_release_state(L);
        return (rc);
    }

    return (OK);
}

/*
 =======================================================================================================================
 * module_init(void):
 * Creates and initializes the Lua states.
 * Called by register_hooks (should pass the pool from there!).
 =======================================================================================================================
 */
static void module_init(apr_pool_t *pool) {

    /*~~*/
    int x;
    /*~~*/

#ifndef _WIN32
    pthread_mutex_init(&Lua_states.mutex, 0);
#endif
    Lua_states.states = (lua_thread *) apr_pcalloc(pool, LUA_STATES * sizeof(lua_thread));
    for (x = 0; x < LUA_STATES; x++) {
        if (!Lua_states.states[x].state) {
            Lua_states.states[x].files = apr_pcalloc(pool, LUA_FILES * sizeof(plua_files));
            lua_init_state(&Lua_states.states[x], x);
        }
    }
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void register_hooks(apr_pool_t *pool) {
    module_init(pool);
    ap_hook_handler(plua_handler, NULL, NULL, APR_HOOK_LAST);
}

/*
 =======================================================================================================================
 * PluaStates N
 * Sets the available amount of Lua states to N states. Default is 50
 =======================================================================================================================
 */
const char *plua_set_LuaStates(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_STATES = x ? x : 50;
    return (NULL);
}

/*
 =======================================================================================================================
 * PluaRuns N
 * Restarts Lua States after N sessions. Default is 500
 =======================================================================================================================
 */
const char *plua_set_LuaRuns(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_RUNS = x ? x : 500;
    return (NULL);
}

/*
 =======================================================================================================================
 * PluaFiles N
 * Sets the file cache array to hold N elements. Default is 200.
 * Each 100 elements take up 30kb of memory, so having 200 elements in 50 states will use 3MB of memory. 
 * If you run a large server with many scripts and domains, you may want to set this to a higher number, fx. 1000.
 =======================================================================================================================
 */
const char *plua_set_LuaFiles(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_FILES = x ? x : 200;
    return (NULL);
}

static const command_rec        my_directives[] =
{
    AP_INIT_TAKE1("PluaStates", plua_set_LuaStates, NULL, OR_ALL, "Sets the number of Lua states to keep open at all times."),
    AP_INIT_TAKE1("PluaRuns", plua_set_LuaRuns, NULL, OR_ALL, "Sets the number of sessions each state can operate before restarting."),
    AP_INIT_TAKE1("PluaFiles", plua_set_LuaFiles, NULL, OR_ALL, "Sets the number of lua scripts to keep cached."),
    { NULL }
};
module AP_MODULE_DECLARE_DATA   plua_module = { STANDARD20_MODULE_STUFF, NULL, NULL, NULL, NULL, my_directives, register_hooks };
