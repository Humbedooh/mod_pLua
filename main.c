/* File: main.c Author: Humbedooh Created on 4. january 2012, 23:30 */
#define LINUX   2
#define _REENTRANT
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <apr-1.0/apr.h>
#include <apache2/httpd.h>
#include <apache2/http_protocol.h>
#include <apache2/http_config.h>
#include <pthread.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#define LUA_COMPAT_MODULE        1
#define PLUA_VERSION             4
static int LUA_STATES  =        50;  /* Keep 50 states open */
static int LUA_RUNS    =       500; /* Restart a state after 500 sessions */

typedef struct
{
    int         working;
    int         sessions;
    request_rec *r;
    lua_State   *state;
    int         typeSet;
    struct
    {
        char    filename[512];
        time_t  modified;
        int     refindex;
    } files[100];
}
lua_thread;
typedef struct
{
    lua_thread*      states;
    pthread_mutex_t mutex;
} lua_states;
static lua_states   Lua_states;

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
#ifdef __WIN32
    if (access(el, 0) == 0) lua_pushboolean(L, 1);
#else
    if (faccessat(0, el, R_OK, AT_EACCESS) == 0) lua_pushboolean(L, 1);
#endif
    else lua_pushboolean(L, 0);
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
    int         y;
    /*~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {

        /*
         * luaL_checktype(L, 1, LUA_TSTRING);
         */
        for (y = 1; y < 100; y++) {
            el = lua_tostring(L, y);
            if (el) ap_rputs(el, thread->r);
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
static int lua_getEnv(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    const char                  *el;
    const char                  *value;
    lua_thread                  *thread;
    const apr_array_header_t    *fields;
    int                         i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        lua_newtable(thread->state);
        fields = apr_table_elts(thread->r->headers_in);

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        apr_table_entry_t   *e = (apr_table_entry_t *) fields->elts;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

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

        // Our own data
        lua_pushstring(thread->state, "Plua-States");
        lua_pushinteger(thread->state, LUA_STATES);
        lua_rawset(L, -3);
        
        lua_pushstring(thread->state, "Plua-Runs");
        lua_pushinteger(thread->state, LUA_RUNS);
        lua_rawset(L, -3);
        
        lua_pushstring(thread->state, "Plua-Version");
        lua_pushinteger(thread->state, PLUA_VERSION);
        lua_rawset(L, -3);
        
        lua_pushstring(thread->state, "Remote-Address");
        lua_pushstring(thread->state, thread->r->connection->remote_ip);
        lua_rawset(L, -3);
        
        lua_pushstring(thread->state, "Lua-Thread");
        lua_pushfstring(thread->state, "%p", thread);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Lua-State");
        lua_pushfstring(thread->state, "%p", thread->state);
        lua_rawset(L, -3);
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
static int util_read(request_rec *r, const char **rbuf) {

    /*~~~~~~~~*/
    int rc = OK;
    /*~~~~~~~~*/

    if ((rc = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR))) {
        return (rc);
    }

    if (ap_should_client_block(r)) {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        char    argsbuffer[HUGE_STRING_LEN];
        int     rsize,
                len_read,
                rpos = 0;
        long    length = r->remaining;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        *rbuf = apr_pcalloc(r->pool, length + 1);

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

#define DEFAULT_ENCTYPE "application/x-www-form-urlencoded"

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_parse_post(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    const char  *data;
    const char  *key,
                *val,
                *type;
    lua_thread  *thread = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        lua_newtable(thread->state);
        if (thread->r->method_number != M_POST) {
            return (0);
        }

        type = apr_table_get(thread->r->headers_in, "Content-Type");
        if (strcasecmp(type, DEFAULT_ENCTYPE) != 0) {
            return (0);
        }

        if (util_read(thread->r, &data) != OK) {
            return (0);
        }

        while (*data && (val = ap_getword(thread->r->pool, &data, '&'))) {
            key = ap_getword(thread->r->pool, &val, '=');
            ap_unescape_url((char *) key);
            ap_unescape_url((char *) val);
            lua_pushstring(thread->state, key);
            lua_pushstring(thread->state, val);
            lua_rawset(L, -3);

            /*
             free((char*) key);
             free((char*) val);
             */
        }

        return (1);
    } else return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_parse_get(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    const char  *data;
    const char  *key,
                *val,
                *type;
    lua_thread  *thread = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        lua_newtable(thread->state);
        data = thread->r->args;
        while (*data && (val = ap_getword(thread->r->pool, &data, '&'))) {
            key = ap_getword(thread->r->pool, &val, '=');
            ap_unescape_url((char *) key);
            ap_unescape_url((char *) val);
            lua_pushstring(thread->state, key);
            lua_pushstring(thread->state, val);
            lua_rawset(L, -3);

            /*
            free((char*) key);
            free((char*) val);
            */
        }

        return (1);
    } else return (0);
}

static const luaL_reg   File_methods[] = { { "stat", lua_fileinfo }, { "exists", lua_fileexists }, { 0, 0 } };
static const luaL_reg   Global_methods[] =
{
    { "echo", lua_echo },
    { "header", lua_header },
    { "setContentType", lua_setContentType },
    { "getEnv", lua_getEnv },
    { "parsePost", lua_parse_post },
    { "parseGet", lua_parse_get },
    { 0, 0 }
};

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
        ap_rprintf(thread->r, "Lua PANIC: %s\n", el);
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
static void register_lua_functions(lua_State *L) {
    lua_atpanic(L, module_lua_panic);
    luaL_register(L, "file", File_methods);
    luaL_register(L, "_G", Global_methods);
}

/*$2
 -----------------------------------------------------------------------------------------------------------------------
    Start Lua state functions
 -----------------------------------------------------------------------------------------------------------------------
 */

/*
 =======================================================================================================================
 * Creates and initializes a new Lua state 
 * @param thread the lua_thread pointer to use @param x The index of the state
    in the grand scheme
 =======================================================================================================================
 */
void lua_init_state(lua_thread *thread, int x) {

    /*~~~~~~~~~~~*/
    lua_State   *L;
    int         y;
    /*~~~~~~~~~~~*/

    thread->state = luaL_newstate();
    thread->sessions = 0;
    L = (lua_State *) thread->state;
    lua_pushinteger(L, x);
    luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_openlibs(L);
    luaopen_debug(L);
    register_lua_functions(L);
    for (y = 0; y < 100; y++) {
        memset(thread->files[y].filename, 0, 512);
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

    pthread_mutex_lock(&Lua_states.mutex);
    for (x = 0; x < LUA_STATES; x++) {
        if (!Lua_states.states[x].working && Lua_states.states[x].state) {
            Lua_states.states[x].working = 1;
            Lua_states.states[x].sessions++;
            L = &Lua_states.states[x];
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&Lua_states.mutex);
    if (found) return (L);
    else {
        sleep(1);
        return (lua_acquire_state());
    }
}

/*
 =======================================================================================================================
 * Releases a Lua state and frees it up for use elsewhere 
 * @param X the lua_State to release
 =======================================================================================================================
 */
void lua_release_state(lua_State *X) {

    /*~~*/
    int x;
    /*~~*/

    lua_gc(X, LUA_GCSTEP, 1);
    pthread_mutex_lock(&Lua_states.mutex);
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

    pthread_mutex_unlock(&Lua_states.mutex);
}

/*$2
 -----------------------------------------------------------------------------------------------------------------------
    End Lua state functions
 -----------------------------------------------------------------------------------------------------------------------
 */

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void module_init(void) {

    /*~~*/
    int x;
    /*~~*/

    pthread_mutex_init(&Lua_states.mutex, 0);
    Lua_states.states = (lua_thread*) calloc(LUA_STATES, sizeof(lua_thread));
    for (x = 0; x < LUA_STATES; x++) {
        if (!Lua_states.states[x].state) {
            lua_init_state(&Lua_states.states[x], x);
        }
    }
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
    int     totalSize = 0;
    size_t  at = 0;
    size_t  pos = 0;
    size_t  inputSize = strlen(input);
    char    X = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    while (at < inputSize) {
        matchStart = strstr((char *) input + at, "<?");
        if (matchStart) {

            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            char    *test = (char *) calloc(1, matchStart - input + at + 20);
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            X = matchStart[0];
            matchStart[0] = 0;
            snprintf(test, matchStart - input + 14, "echo([=[%s]=]);\0", input + at);
            matchStart[0] = X;

            
    //        ap_rprintf(thread->r, "Adding raw data: <pre>%s</pre><br/>", ap_escape_html(thread->r->pool, test));
             
            lua_add_code(&output, test);
            free(test);
            at = (matchStart - input) + 2;
            matchEnd = strstr((char *) matchStart + 2, "?>");
            if (!matchEnd) matchEnd = (input + strlen(input));
            if (matchEnd) {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                char    *test = (char *) calloc(1, matchEnd - matchStart+2);
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                strncpy(test, matchStart + 2, matchEnd - matchStart - 2);
                test[matchEnd - matchStart-2] = 0;
                at = matchEnd - input + 2;

               
  //              ap_rprintf(thread->r, "Adding code: <pre>%s</pre><br/>", ap_escape_html(thread->r->pool, test));
                 
                lua_add_code(&output, test);
                free(test);
            }
        } else {
            if (inputSize > at) {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                char    *test = (char *) calloc(1, strlen(input) - at + 20);
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                snprintf(test, strlen(input) - at + 14, "echo([=[%s]=]);\0", input + at);
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

    stat(thread->r->filename, &statbuf);
    for (x = 0; x < 100; x++) {
        if (!strcmp(thread->files[x].filename, thread->r->filename)) {
            if (statbuf.st_mtim.tv_sec != thread->files[x].modified) {

                /*
                 * ap_rprintf(thread->r,"Deleted out-of-date compiled version at index %u", x);
                 */
                memset(thread->files[x].filename, 0, 512);
                luaL_unref(thread->state, LUA_REGISTRYINDEX, thread->files[x].refindex);
                break;
            } else {

                /*
                 * ap_rprintf(thread->r,"Found usable compiled version at index %u", x);
                 */
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
            iBuffer = (char *) calloc(1,iSize+1);
            fread(iBuffer, iSize, 1, input);
            fclose(input);
            rc = lua_parse_file(thread, iBuffer);
            free(iBuffer);
            if (rc == LUA_ERRSYNTAX) {
                return (-2);
            }

            if (!rc) {
                x = luaL_ref(thread->state, LUA_REGISTRYINDEX);

                /*
                 * ap_rprintf(thread->r,"Pushed the string onto the registry at index %u<br/>", x);
                 * * Put the compiled file into the files struct
                 */
                for (y = 0; y < 100; y++) {
                    if (!strlen(thread->files[y].filename)) {
                        strcpy(thread->files[y].filename, thread->r->filename);
                        thread->files[y].modified = statbuf.st_mtim.tv_sec;
                        thread->files[y].refindex = x;

                        /*
                         * ap_rprintf(thread->r,"Pushed the into the file list at index %u<br/>", y);
                         */
                        break;
                    }
                }

                return (x);
            }
        } else return (-1);
    }
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int plua_handler(request_rec *r) {

    /*~~~~~~~~~~~~~~~~~~~*/
    int         exists = 1;
    struct stat statbuf;
    /*~~~~~~~~~~~~~~~~~~~*/

    if (!r->handler || strcmp(r->handler, "plua")) return (DECLINED);
    if (r->method_number != M_GET && r->method_number != M_POST) return (HTTP_METHOD_NOT_ALLOWED);
    if (stat(r->filename, &statbuf) == -1) exists = 0;
    else if S_ISDIR(statbuf.st_mode)
    exists = 0;
    if (!exists) {
        ap_set_content_type(r, "text/html;charset=ascii");
        ap_rputs("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n", r);
        ap_rputs("<html><head><title>Lua_HTML: Error</title></head>", r);
        ap_rprintf(r, "<body><h1>No such file: %s</h1></body></html>", r->unparsed_uri);
        return (HTTP_NOT_FOUND);
    } else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        int         x = 0,
                    rc = 0;
        void        *p = 0;
        lua_thread  *l = lua_acquire_state();
        lua_State   *L = l->state;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        l->r = r;
        lua_pushlightuserdata(L, l);
        x = luaL_ref(L, LUA_REGISTRYINDEX);
        rc = lua_compile_file(l, r->filename);
        if (rc < 1) {

            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            const char    *err = lua_tostring(L, -1);
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            err = err ? err : "(nil)";
            ap_set_content_type(r, "text/html;charset=ascii");
            ap_rprintf(r, "<h3>Compiler error in %s:</h3><pre>%s</pre>", r->filename, err);
        } else {

            /*$2
             -----------------------------------------------------------------------------------------------------------
                Push the request handle into the table as t[0].
             -----------------------------------------------------------------------------------------------------------
             */

            lua_rawgeti(L, LUA_REGISTRYINDEX, rc);
            l->typeSet = 0;
            if (lua_pcall(L, 0, LUA_MULTRET, 0)) {
                ap_set_content_type(r, "text/html;charset=ascii");
                ap_rprintf(r, "<h3>Run-time error:</h3><pre>%s</pre>", lua_tostring(L, -1));
            } else {
                if (l->typeSet == 0) ap_set_content_type(r, "text/html;charset=ascii");

                /*
                 * ap_rprintf(r, "<b>Compiled and ran fine from index %u</b>", rc);
                 */
            }
        }

        /* Cleanup */
        luaL_unref(L, LUA_REGISTRYINDEX, x);
        lua_release_state(L);

        /*
         * lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
         * *p = lua_touserdata(L, -1);
         * *ap_rprintf(r, "<b>Got it back as %lu\n</b>", p);
         * * do stuf
         */
    }

    return (OK);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void register_hooks(apr_pool_t *pool) {
    module_init();
    ap_hook_handler(plua_handler, NULL, NULL, APR_HOOK_LAST);
}

const char* plua_set_LuaStates(cmd_parms* cmd, void* cfg, const char* arg) {
    int x = atoi(arg);
    LUA_STATES = x ? x : 50;
    return NULL;
}

const char* plua_set_LuaRuns(cmd_parms* cmd, void* cfg, const char* arg) {
    int x = atoi(arg);
    LUA_RUNS = x ? x : 500;
    return NULL;
}

static const command_rec my_directives[] = {
  AP_INIT_TAKE1("PluaStates", plua_set_LuaStates, NULL, OR_ALL, "Sets the number of Lua states to keep open at all times."),
  AP_INIT_TAKE1("PluaRuns", plua_set_LuaRuns, NULL, OR_ALL, "Sets the number of sessions each state can operate before restarting."),
  { NULL }
} ;

module AP_MODULE_DECLARE_DATA   plua_module = { 
    STANDARD20_MODULE_STUFF, 
    NULL,
    NULL, 
    NULL, 
    NULL, 
    my_directives, 
    register_hooks };
