/*
 * File: main.c Author: Humbedooh Created on 4. january 2012, 23:30 ;
 * Include the headers
 */
#include "mod_plua.h"

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua hooks and handlers
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
    register_hooks(apr_pool_t *pool): The basic hook registering function, as seen on TV.
 =======================================================================================================================
 */
static void register_hooks(apr_pool_t *pool) {

    /*~~*/
    int x;
    /*~~*/

    for (x = 0; x < PLUA_RAW_TYPES; x++) {
        pLua_rawTypes[x] = (char *) apr_pcalloc(pool, 64);
    }
    memset(LUA_IGNORE, 0, 256);

    /* Hook initialization of global variables to the child init stage */
    ap_hook_child_init(module_init, NULL, NULL, APR_HOOK_MIDDLE);

    /* Hook the file handler */
    ap_hook_handler(plua_handler, NULL, NULL, APR_HOOK_LAST);
}

/*
 =======================================================================================================================
    module_init(void): Creates and initializes the Lua states. Called by register_hooks (should pass the pool from
    there!).
 =======================================================================================================================
 */
static void module_init(apr_pool_t *pool, server_rec *s) {

    /*~~~~~~~~~~~~~~~~~*/
    pLuaClock   aprClock,
                cpuClock;
    /*~~~~~~~~~~~~~~~~~*/

    /*
     * Get the difference between apt_time_now and the native clock function if any. This is, at present,
     * only really needed by Windows
     */
    aprClock = pLua_getClock(1);
    cpuClock = pLua_getClock(0);
    pLua_clockOffset.seconds = aprClock.seconds - cpuClock.seconds;
    pLua_clockOffset.nanoseconds = aprClock.nanoseconds - cpuClock.nanoseconds;

    /* Initialize the overall domain/state pool and its mutex */
    LUA_BIGPOOL = pool;
    pthread_mutex_init(&pLua_bigLock, 0);
    pLua_domainsAllocated = 1;
    pLua_domains = calloc(2, sizeof(lua_domain));
    pLua_threads = apr_pcalloc(pool, (LUA_STATES + 1) * sizeof(lua_threadStates));

    /* This initial domain pool is used for when pLuaMultiDomain is set to 0. */
    pthread_mutex_init(&pLua_domains[0].mutex, 0);
    pLua_domains[0].pool = pool;
    sprintf(pLua_domains[0].domain, "localDomain");
    pLua_init_states(&pLua_domains[0]);

    /* apr_dbd initialization of persistent database objects */
    apr_dbd_init(pool);
}

/*
 =======================================================================================================================
    pLua_handler(request_rec* r): The handler for events set to be handled by 'pLua' in apache. It parses, compiles,
    caches and runs the files using all the functions above.
 =======================================================================================================================
 */
static int plua_handler(request_rec *r) {

    /*~~~~~~~~~~~~~~~~~~~*/
    int     exists = 1;
    char    compileRaw = 0;
    int     UID = 0;
    /*~~~~~~~~~~~~~~~~~~~*/

    /* Check if we are being called, and that the request method is one we can handle. */
    if (!r->handler || strcmp(r->handler, "plua")) return (DECLINED);
    if (r->method_number != M_GET && r->method_number != M_POST) return (HTTP_METHOD_NOT_ALLOWED);

    /* Check if the file requested really exists. */
    exists = ((r->finfo.filetype != APR_NOFILE) && !(r->finfo.filetype & APR_DIR));

    /* If no file (or if it's a folder), print out the 404 message. */
    if (!exists) return (HTTP_NOT_FOUND);
    else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        char        *xStart = 0;
        char        *xEnd = 0;
        int         x = 0,
                    rc = 0;
        lua_thread  *l = lua_acquire_state(r, r->server->server_hostname);
        lua_State   *L;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        /* Check if state acuisition worked */
        if (!l) return (HTTP_INTERNAL_SERVER_ERROR);
        
        /* Set up the lua_thread struct and change to the current directory. */
        L = l->state;
        l->r = r;
        l->debugging = 0;
        l->errorLevel = LUA_PERROR;
#ifndef _WIN32
        rc = chdir(getPWD(l));
#else
        SetCurrentDirectoryA(getPWD(l));
#endif

        /* Set default return code to OK (200-ish) and reset the parse counter */
        l->returnCode = OK;
        l->parsedPost = 0;

        /* Check if we want to compile this file as a plain lua file or not */
        xEnd = r->filename;
        xStart = strchr(r->filename, '.');
        while (xStart != 0) {
            xEnd = xStart;
            xStart = strchr(xEnd + 1, '.');
        }

        for (x = 0; x < PLUA_RAW_TYPES; x++) {
            if (strlen(pLua_rawTypes[x])) {
                if (!strcmp(xEnd, pLua_rawTypes[x])) {
                    compileRaw = 1;
                    break;
                }
            } else break;
        }

        /* Call the compiler function and let it either compile or read from cache. */
        rc = lua_compile_file(l, r->filename, &r->finfo, compileRaw);

        /* Compiler error? */
        if (rc < 1) {
            pLua_print_error(l, "Compiler error", r->filename);
            rc = OK;

            /* No compiler error, let's run this file. */
        } else {

            /* Get the compiled binary chunk off the registry. */
            lua_rawgeti(L, LUA_REGISTRYINDEX, rc);

            /* Set content-type to default. */
            l->typeSet = 0;
            rc = 0;

            /* If script timeout or memory limit is in effect, attach the debug hook for monitoring. */
            if (LUA_TIMEOUT > 0 || LUA_MEM_LIMIT > 0) {
                l->runTime = time(0);
                l->debugging = 1;
                lua_sethook(L, pLua_debug_hook, LUA_MASKLINE | LUA_MASKCOUNT, 1);
            }

            rc = lua_pcall(L, 0, LUA_MULTRET, 0);

            /* DId we get a run-time error? */
            if (rc) {
                pLua_print_error(l, "Run-time error", r->filename);
                rc = l->returnCode;

                /* No error, everything went fine, set up the content type if needed and return OK. */
            } else {
                if (l->typeSet == 0) ap_set_content_type(r, "text/html");
                rc = l->returnCode;
            }

            /* Remove the debug hook if set */
            if (l->debugging) {
                lua_sethook(L, pLua_debug_hook, 0, 0);
            }
        }

        /* Cleanup */
        lua_release_state(l);
        return (rc);
    }

    return (OK);
}

/*
 =======================================================================================================================
    pLua_getClock(char useAPR): Gets the current time with microsecond precision. if useAPR is set to 1, it will always
    use apr_time_now() instead of platform specific functions.
 =======================================================================================================================
 */
static pLuaClock pLua_getClock(char useAPR)
{
    /*~~~~~~~~~~~~~~~~~~~~~~*/
#ifdef _WIN32
    LARGE_INTEGER   cycles;
    LARGE_INTEGER   frequency;
#else
    apr_time_t      now;
#endif
    pLuaClock       cstr;
    /*~~~~~~~~~~~~~~~~~~~~~~*/

#ifdef _WIN32
    if (!useAPR) {
        QueryPerformanceCounter(&cycles);
        QueryPerformanceFrequency(&frequency);
        cstr.seconds = cycles.QuadPart / frequency.QuadPart;
        cstr.nanoseconds = cycles.QuadPart % (cycles.QuadPart * (1000000000 / frequency.QuadPart));
    } else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        apr_time_t  now = apr_time_now();
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        cstr.seconds = now / 1000000;
        cstr.nanoseconds = (now % 1000000) * 1000;
    }

#else
    now = apr_time_now();
    cstr.seconds = now / 1000000;
    cstr.nanoseconds = (now % 1000000) * 1000;
#endif
    return (cstr);
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua compiler and parser functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
    pLua_print_error(lua_thread *thread, const char *type, const char* filename): Prints out a mod_pLua error message
    of type _type_ to the remote client. This does not necessarilly halt any ongoing execution of code.
 =======================================================================================================================
 */
static void pLua_print_error(lua_thread *thread, const char *type, const char *filename) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    char        *errX;
    const char  *err = lua_tostring(thread->state, -1);
    int         x = 0;
    char        found[8],
                *lineX,
                *where;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (err && !strstr(err, "MOD_PLUA_EXIT")) {
        if (thread->errorLevel == 0) return;
        errX = ap_escape_html(thread->r->pool, err);
        for (x = 1; x < 2048; x++) {
            sprintf(found, ":%u: ", x);
            if ((where = strstr(errX, found)) != 0) {
                lineX = (char *) apr_pcalloc(thread->r->pool, strlen(errX) + 50);
                if (lineX) {
                    sprintf(lineX, "On line <code style='font-weight: bold; color:#774411;'>%u:</code> %s", x, where + strlen(found));
                    errX = lineX;
                }
                break;
            }
        }

#if (AP_SERVER_MINORVERSION_NUMBER <= 2)
        if (LUA_LOGLEVEL >= 3) ap_log_rerror(filename, 0, APLOG_ERR, APR_EGENERAL, thread->r, "in %s: %s", filename, err);
#else
        if (LUA_LOGLEVEL >= 3) ap_log_rerror(filename, 0, 0, APLOG_ERR, APR_EGENERAL, thread->r, "in %s: %s", filename, err);
#endif
        ap_set_content_type(thread->r, "text/html; charset=ascii");
        filename = filename ? filename : "";
        ap_rprintf(thread->r, pLua_error_template, type, filename ? filename : "??", errX ? errX : err);
    }
}

/*
 =======================================================================================================================
    module_lua_panic(lua_State *L): The Lua panic handler.
 =======================================================================================================================
 */
static int module_lua_panic(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    lua_thread  *thread = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 0);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) {
        pLua_print_error(thread, "Lua PANIC", 0);
    } else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        const char  *el = lua_tostring(L, 1);
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        fprintf(stderr, "Lua PANIC: %s\n", el ? el : "(nil)");
    }

    lua_settop(L, 0);
    return (0);
}

/*
 =======================================================================================================================
    pLua_get_thread(lua_State *L): Gets the pLua thread handle from the Lua stack.
 =======================================================================================================================
 */
static lua_thread *pLua_get_thread(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    lua_thread  *thread = 0;
    int         y;
    /*~~~~~~~~~~~~~~~~~~~~*/

    lua_rawgeti(L, LUA_REGISTRYINDEX, 0);
    thread = (lua_thread *) lua_touserdata(L, -1);
    if (thread) return (thread);

    /*
     * This should always work, but some modules are known to break it! ;
     * So in case it fails, we have a backup plan.
     */
    else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        /* Check the backup list! */
        apr_os_thread_t me = apr_os_thread_current();
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        for (y = 0; y < LUA_STATES; y++) {
            if (pLua_threads[y].thread == me) {
                return (pLua_threads[y].state);
            }
        }

        fprintf(stderr,
                "mod_pLua: Could not obtain the mod_pLua handle from the Lua registry index. This may be caused by mod_pLua being used with an incompatible Lua library.\r\n");
    }

    return (0);
}

/*
 =======================================================================================================================
    pLua_debug_hook(lua_State *L, lua_Debug *ar): The debug hook for monitoring execution time and, in case a timeout
    limit is set, aborting the script if it exceeds this limit.
 =======================================================================================================================
 */
static void pLua_debug_hook(lua_State *L, lua_Debug *ar) {

    /*~~~~~~~~~~~~~~~~*/
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    then++;
    if ((then % 200) == 0) {
        
        /* Script timeout testing */
        if (LUA_TIMEOUT > 0) {
            /*~~~~~~~~~~~~~~~~~~*/
            time_t  now = time(0);
            /*~~~~~~~~~~~~~~~~~~*/

            thread = pLua_get_thread(L);
            if (thread && (now - thread->runTime) > LUA_TIMEOUT)
                luaL_error(L, "The script took too long to execute (timed out)!\n", "the script...somewhere!");
        }
        
        /* Memory limit testing */
        if (LUA_MEM_LIMIT > 0 ) {
            int kb = lua_gc(L, LUA_GCCOUNT, 0);
            
            // If, at first, we've hit the limit, attempt to GC our way out of it.
            if ( kb > LUA_MEM_LIMIT ) {
                lua_gc(L, LUA_GCCOLLECT, 0);
                kb = lua_gc(L, LUA_GCCOUNT, 0);
                // If this didn't work, it's time to b0rk
                if ( kb > LUA_MEM_LIMIT ) luaL_error(L, "Memory limit reached!\n", "the script...somewhere!");
            }
        }
    }
}

/*
 =======================================================================================================================
    lua_add_code(char **buffer, const char *string): Adds chunks of code to the final buffer, expanding it in memory as
    it grows.
 =======================================================================================================================
 */
void lua_add_code(char **buffer, const char *string) {

    /*~~~~~~~~~~~~~~~~~*/
    char    *b = *buffer;
    /*~~~~~~~~~~~~~~~~~*/

    if (!string || !strlen(string)) return;
    if (!b) {
        b = (char *) calloc(1, strlen(string) + 2);
        strcpy(b, string);
        b[strlen(string)] = ' ';
    } else {

        /*~~~~~~~~~~~*/
        size_t  at = 0;
        /*~~~~~~~~~~~*/

        at = strlen(b);
        b = (char *) realloc(b, at + strlen(string) + 3);
        b[at + strlen(string) + 1] = ' ';
        b[at + strlen(string) + 2] = 0;
        strcpy((char *) (b + at), string);
        strcpy((char *) (b + at + strlen(string)), " ");
    }

    *buffer = b;
    return;
}

/*
 =======================================================================================================================
    lua_parse_file(lua_thread *thread, char *input): Splits up the string given by input and looks for <? ... ?>
    segments. These, and their surrounding HTML code, are then added, bit by bit, to the final code string.
 =======================================================================================================================
 */
int lua_parse_file(lua_thread *thread, char *input) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    int         rc = 0;
    char        *output = 0;
    char        *matchStart,
                *pmatchStart,
                *matchEnd;
    size_t      at = 0;
    size_t      inputSize = strlen(input);
    char        X = 0;
    int         i = 0;
    const char  *sTag,
                *eTag;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    while (at < inputSize) {
        pmatchStart = 0;
        for (i = 0; pLua_file_tags[i].sTag != 0; i++) {
            if (LUA_SHORTHAND == 0) {
                if (!strcmp(pLua_file_tags[i].sTag, "<?")) continue;
            }
            matchStart = strstr((char *) input + at, pLua_file_tags[i].sTag);
            if (matchStart && ( (matchStart < pmatchStart) || pmatchStart == 0) ) {
                sTag = pLua_file_tags[i].sTag;
                eTag = pLua_file_tags[i].eTag;
                pmatchStart = matchStart;
                //break;
            }
            else {
                matchStart = pmatchStart;
            }
        }

        if (matchStart) {

            /* Add any preceding raw html as an echo */
            if (matchStart - input > 0) {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                char    *test = (char *) apr_pcalloc(thread->r->pool, matchStart - input + at + 20);
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                X = matchStart[0];
                matchStart[0] = 0;
#ifdef _WIN32
                _snprintf_c(test, matchStart - input + 18, "echo([===[%s]===]);", input + at);
#else
                snprintf(test, matchStart - input + 18, "echo([===[%s]===]);", input + at);
#endif
                matchStart[0] = X;

                /*
                 * ap_rprintf(thread->r, "Adding raw data: <pre>%s</pre><br/>", ap_escape_html(thread->r->pool, test));
                 */
                lua_add_code(&output, test);
            }

            /* Find the beginning and end of the pLua chunk */
            at = (matchStart - input) + strlen(sTag);
            matchEnd = strstr((char *) matchStart + strlen(sTag), eTag);
            if (!matchEnd) matchEnd = (input + strlen(input));
            if (matchEnd) {

                /*~~~~~~~~~~~~~~~*/
                char    *test = 0;
                char    *etest = 0;
                /*~~~~~~~~~~~~~~~*/

                /* <?=variable?> check */
                if (matchStart[strlen(sTag)] == '=') {
                    test = (char *) apr_pcalloc(thread->r->pool, matchEnd - matchStart + strlen(sTag) + 10);
                    etest = (char *) apr_pcalloc(thread->r->pool, matchEnd - matchStart + 20);
                    strncpy(test, matchStart + 1 + strlen(sTag), matchEnd - matchStart - strlen(sTag) - 1);
                    test[matchEnd - matchStart - 1 - strlen(sTag)] = 0;
                    at = matchEnd - input + strlen(eTag);
                    sprintf(etest, "echo(%s);", test);

                    /*
                     * ap_rprintf(thread->r, "Adding echo-code: <pre>echo(%s)</pre><br/>",
                     * ap_escape_html(thread->r->pool, test));
                     */
                    lua_add_code(&output, etest);
                } /* <? code ?> check */ else {
                    test = (char *) apr_pcalloc(thread->r->pool, matchEnd - matchStart + strlen(sTag) + 10);
                    strncpy(test, matchStart + strlen(sTag), matchEnd - matchStart - strlen(sTag));
                    test[matchEnd - matchStart - strlen(sTag)] = 0;
                    at = matchEnd - input + strlen(eTag);

                    /*
                     * ap_rprintf(thread->r, "Adding code: <pre>%s</pre><br/>", ap_escape_html(thread->r->pool, test));
                     */
                    lua_add_code(&output, test);
                }
            }
        } else {
            if (inputSize > at) {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                char    *test = (char *) apr_pcalloc(thread->r->pool, strlen(input) - at + 32);
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#ifdef _WIN32
                _snprintf(test, strlen(input) - at + 18, "echo([===[%s]===]);", input + at);
#else
                snprintf(test, strlen(input) - at + 18, "echo([===[%s]===]);", input + at);
#endif
                at = inputSize;
                lua_add_code(&output, test);
            }
        };
    }

    rc = luaL_loadstring(thread->state, output ? output :
                             "echo('<h4>Empty file!</h4>This script file is either empty or was being written to at the time of execution.');");
    if (output) free(output);
    return (rc);
}

/*
 =======================================================================================================================
    lua_compile_file(lua_thread *thread, const char *filename, struct stat *statbuf): Compiles the filename given. It
    first checks if the file is available in the cache, and if so, hasn't been modified on disk since. If available, it
    returns the handle (reference number) do the precompiled code, otherwise it parses and compiles the new code and
    stores it in the cache.
 =======================================================================================================================
 */
int lua_compile_file(lua_thread *thread, const char *filename, apr_finfo_t *statbuf, char rawCompile) {

    /*~~~~~~~~~~~~~~~*/
    FILE    *input = 0;
    char    *iBuffer;
    size_t  iSize;
    int     rc = 0;
    int     x = 0,
            y = 0;
    int     found = 0;
    /*~~~~~~~~~~~~~~~*/

    /* For each file on record, check if the names match */
    for (x = 0; x < LUA_FILES; x++) {
        if ((thread->files[x].filename[0] == 0)) break; /* No filename means end of the used portion of the list. */

        /* Do we have a match? */
        if (!strcmp(thread->files[x].filename, filename)) {

            /* Is the cached file out of date? */
            if (statbuf->mtime != thread->files[x].modified || statbuf->size != thread->files[x].size) {
                memset(thread->files[x].filename, 0, 256);
                luaL_unref(thread->state, LUA_REGISTRYINDEX, thread->files[x].refindex);
                break;

                /* Did we find a useable copy? */
            } else {
                found = thread->files[x].refindex;
                return (found);
            }
        }
    }

    /* If no useable copy is found, preprocess and compile the file from scratch. */
    if (!found) {

        /*
         * Straight forward;
         * Read the file
         */
        input = fopen(filename, "r");
        if (input) {
            fseek(input, 0, SEEK_END);
            iSize = ftell(input);
            fseek(input, 0, SEEK_SET);
            iSize = iSize ? iSize : 1;
            iBuffer = (char *) calloc(1, iSize + 1);
            iSize = fread(iBuffer, iSize, 1, input);
            fclose(input);

            /* Hand the file data over to lua_parse_file and preprocess the html and code. */
            if (!rawCompile) {
                rc = lua_parse_file(thread, iBuffer);
            } else {
                if (iBuffer) {

                    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                    const char  *shebang = strstr(iBuffer, "#!");
                    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                    if (shebang && shebang == iBuffer) {
                        shebang = strchr(iBuffer, '\n');
                        rc = luaL_loadstring(thread->state, shebang);
                    } else {
                        rc = luaL_loadstring(thread->state, iBuffer);
                    }
                } else {
                    rc = luaL_loadstring(thread->state, "echo('No input specified or file was written to at time of execution')");
                }
            }

            free(iBuffer);

            /* Did we encounter a syntax error while parsing? (I think not, how could we?) */
            if (rc == LUA_ERRSYNTAX) {
                return (-2);
            }

            /* Save the compiled binary string in the Lua registry for later use. */
            if (!rc) {

                /*~~~~~~~~~~~~~~*/
                int foundSlot = 0;
                /*~~~~~~~~~~~~~~*/

                /* Push the binary chunk onto the Lua registry and save the reference */
                x = luaL_ref(thread->state, LUA_REGISTRYINDEX);
                if (PLUA_DEBUG) ap_rprintf(thread->r, "Pushed the string from %s onto the registry at index %u<br/>", filename, x);

                /* Look for an empty slot in our file cache to save this reference. */
                for (y = 0; y < LUA_FILES; y++) {
                    if (!strlen(thread->files[y].filename)) {
                        strcpy(thread->files[y].filename, filename);
                        thread->files[y].modified = statbuf->mtime;
                        thread->files[y].size = statbuf->size;
                        thread->files[y].refindex = x;
                        foundSlot = 1;
                        thread->youngest = y;
                        if (PLUA_DEBUG) ap_rprintf(thread->r, "New file: Pushed it into the file list at index %u<br/>", y);
                        break;
                    }
                }

                /* If no empty spots, use the oldest spot for this new chunk. */
                if (!foundSlot) {

                    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                    int y = (thread->youngest + 1) % LUA_FILES;
                    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                    /* Unref whatever was in this slot. */
                    luaL_unref(thread->state, LUA_REGISTRYINDEX, thread->files[y].refindex);

                    /* Update the slot with new info. */
                    thread->youngest = y;
                    strcpy(thread->files[y].filename, filename);
                    thread->files[y].modified = statbuf->mtime;
                    thread->files[y].refindex = x;
                    if (PLUA_DEBUG) ap_rprintf(thread->r, "Pushed the into the file list at index %u, replacing an old file<br/>", y);
                }

                /* Return the reference number */
                return (x);
            }
        } else return (-1);
    }

    return (-1);
}

/*
 =======================================================================================================================
    getPWD(lua_thread *thread): Gets the current working directory of the requested script.
 =======================================================================================================================
 */
char *getPWD(lua_thread *thread) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    size_t  x,
            y,
            z;
    char    *pwd = (char *) apr_pcalloc(thread->r->pool, strlen(thread->r->filename) + 2);
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
    } else return (".");
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua Lua functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
    lua_fileexists(lua_State *L): file.exists(filename): Returns true if a file/folder exists, false otherwise.
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

/*
 =======================================================================================================================
    lua_unlink(lua_State *L): file.unlink(filename): Unlinks (deletes) a given file.
 =======================================================================================================================
 */
static int lua_unlink(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~*/
    const char  *filename;
    int         rc = 0;
    /*~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    filename = lua_tostring(L, 1);
    lua_settop(L, 0);
    rc = unlink(filename);
    if (rc) lua_pushboolean(L, 0);
    else lua_pushboolean(L, 1);
    return (1);
}

/*
 =======================================================================================================================
    lua_rename(lua_State *L): file.rename(oldfile, newfile): Renames/moves a file to a new location.
 =======================================================================================================================
 */
static int lua_rename(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~*/
    const char  *filename,
                *newfilename;
    int         rc = 0;
    /*~~~~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    luaL_checktype(L, 2, LUA_TSTRING);
    filename = lua_tostring(L, 1);
    newfilename = lua_tostring(L, 1);
    lua_settop(L, 0);
    rc = rename(filename, newfilename);
    if (rc) lua_pushboolean(L, 0);
    else lua_pushboolean(L, 1);
    return (1);
}

/*
 =======================================================================================================================
    lua_exit(lua_State *L): Stops execution of the script.
 =======================================================================================================================
 */
static int lua_exit(lua_State *L) {
    luaL_error(L, "MOD_PLUA_EXIT");
    return (0);
}

/*
 =======================================================================================================================
    lua_dbclose(lua_State *L): db:close(): Closes an open database connection.
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
    if (db && db->alive) {
        if (db->type == 0) { rc = apr_dbd_close(db->driver, db->handle); }
        else { rc = 0; }
        db->driver = 0;
        db->handle = 0;
        db->alive = 0;
        if (db->pool && db->type == 0) apr_pool_destroy(db->pool);
    }

    lua_settop(L, 0);
    lua_pushnumber(L, rc);
    return (1);
}

/*
 =======================================================================================================================
    lua_dbgc(lua_State *L): db:__gc(): Garbage collecing function.
 =======================================================================================================================
 */
static int lua_dbgc(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    dbStruct    *db = 0;
    /*~~~~~~~~~~~~~~~~*/

    /*
     * apr_status_t rc = 0;
     */
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, 1, 0);
    luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
    db = (dbStruct *) lua_topointer(L, -1);
    if (db && db->alive) {
        if (db->type == 0) { apr_dbd_close(db->driver, db->handle); }
        db->driver = 0;
        db->handle = 0;
        db->alive = 0;
        if (db->pool && db->type == 0) apr_pool_destroy(db->pool);
    }

    lua_settop(L, 0);
    return (0);
}

/*
 =======================================================================================================================
    lua_dbhandle(lua_State *L): db:active(): Returns true if the connection to the db is still active, false otherwise.
 =======================================================================================================================
 */
static int lua_dbhandle(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, 1, 0);
    luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
    db = (dbStruct *) lua_topointer(L, -1);
    if (db && db->alive) {
        rc = apr_dbd_check_conn(db->driver, db->pool, db->handle);
        if (rc == APR_SUCCESS) {
            lua_pushboolean(L, 1);
            return (1);
        }
    }

    lua_pushboolean(L, 0);
    return (1);
}

/*
 =======================================================================================================================
    lua_dbdo(lua_State *L): db:run(query): Executes the given database query and returns the number of rows affected.
    If an error is encountered, returns nil as the first parameter and the error message as the second.
 =======================================================================================================================
 */
static int lua_dbdo(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    int             x = 0;
    const char      *statement;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TSTRING);
    statement = lua_tostring(L, 2);
    lua_rawgeti(L, 1, 0);
    luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
    db = (dbStruct *) lua_topointer(L, -1);
    if (db && db->alive) {
        rc = apr_dbd_query(db->driver, db->handle, &x, statement);
    } else {
        rc = 0;
        x = -1;
    }

    if (rc == APR_SUCCESS) lua_pushnumber(L, x);
    else {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        const char  *err = apr_dbd_error(db->driver, db->handle, rc);
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        lua_pushnil(L);
        if (err) {
            lua_pushstring(L, err);
            return (2);
        }
    }

    return (1);
}

/*
 =======================================================================================================================
    lua_dbescape(lua_State *L): db:escape(string): Escapes a string for safe use in the given database type.
 =======================================================================================================================
 */
static int lua_dbescape(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~*/
    dbStruct    *db = 0;
    lua_thread  *thread;
    const char  *statement;
    const char  *escaped = 0;
    /*~~~~~~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TSTRING);
        statement = lua_tostring(L, 2);
        lua_rawgeti(L, 1, 0);
        luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
        db = (dbStruct *) lua_topointer(L, -1);
        if (db && db->alive) {
            apr_dbd_init(thread->r->pool);
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
    lua_dbquery(lua_State *L): db:query(statement): Queries the database for the given statement and returns the
    rows/columns found as a table. If an error is encountered, returns nil as the first parameter and the error message
    as the second.
 =======================================================================================================================
 */
static int lua_dbquery(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    lua_thread      *thread;
    const char      *statement;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TSTRING);
        statement = lua_tostring(L, 2);
        lua_rawgeti(L, 1, 0);
        luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);
        db = (dbStruct *) lua_topointer(L, -1);
        if (db && db->alive) {

            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            int                 rows,
                                cols;
            apr_dbd_results_t   *results = 0;
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            apr_dbd_init(thread->r->pool);
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
            } else {

                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
                const char  *err = apr_dbd_error(db->driver, db->handle, rc);
                /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

                lua_pushnil(L);
                if (err) {
                    lua_pushstring(L, err);
                    return (2);
                }
            }
        }

        lua_pushboolean(L, 0);
        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_dbopen(lua_State *L): dbopen(dbType, dbString): Opens a new connection to a database of type _dbType_ and with
    the connection parameters _dbString_. If successful, returns a table with functions for using the database handle.
    If an error occurs, returns nil as the first parameter and the error message as the second. See the APR_DBD for a
    list of database types and connection strings supported.
 =======================================================================================================================
 */
static int lua_dbopen(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    const char      *type;
    const char      *arguments;
    const char      *error = 0;
    lua_thread      *thread;
    dbStruct        *db = 0;
    apr_status_t    rc = 0;
    apr_pool_t      *pool = 0;
    ap_dbd_t*       dbdhandle = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        
        apr_pool_create(&pool, thread->bigPool);
        db = (dbStruct *) apr_pcalloc(pool, sizeof(dbStruct));
        
        db->alive = 0;
        db->pool = pool;
        db->type = 0; // 0 for regular, 1 for mod_dbd
        apr_dbd_init(pool);
        luaL_checktype(L, 1, LUA_TSTRING);
        luaL_checktype(L, 2, LUA_TSTRING);
        type = lua_tostring(L, 1);
        arguments = lua_tostring(L, 2);
        lua_settop(L, 0);

        /*
         * apr_dbd_init(thread->r->pool);
         */
        if (!strcmp(type, "mod_dbd")) {
            db->type = 1;
#if (_WITH_MOD_DBD == 1)
            dbdhandle = ap_dbd_acquire(thread->r);
#endif
            if (dbdhandle) {
                    db->alive = 1;
                    db->driver = dbdhandle->driver;
                    db->handle = dbdhandle->handle;
                    lua_newtable(L);
                    /* Create metatable for __gc function */
                    luaL_newmetatable(L, "pLua.dbopen");
                    lua_pushliteral(L, "__gc");
                    lua_pushcfunction(L, lua_dbgc);
                    lua_rawset(L, -3);
                    lua_setmetatable(L, -2);

                    /* Register db functions */
                    luaL_register(L, NULL, db_methods);

                    /* lua_newuserdata() */
                    lua_pushlightuserdata(L, db);
                    lua_rawseti(L, -2, 0);
                    return (1);
                } else {
                    if (pool) apr_pool_destroy(pool);
                    lua_pushnil(L);
                    lua_pushliteral(L, "This module was not compiled with mod_dbd support.");
                    return (2);
                }
            }
            else {
                rc = apr_dbd_get_driver(db->pool, type, &db->driver);
                if (rc == APR_SUCCESS) {
                    if (strlen(arguments)) {
                        rc = apr_dbd_open_ex(db->driver, db->pool, arguments, &db->handle, &error);
                        if (rc == APR_SUCCESS) {
                            db->alive = 1;
                            lua_newtable(L);

                            /* Create metatable for __gc function */
                            luaL_newmetatable(L, "pLua.dbopen");
                            lua_pushliteral(L, "__gc");
                            lua_pushcfunction(L, lua_dbgc);
                            lua_rawset(L, -3);
                            lua_setmetatable(L, -2);

                            /* Register db functions */
                            luaL_register(L, NULL, db_methods);

                            /* lua_newuserdata() */
                            lua_pushlightuserdata(L, db);
                            lua_rawseti(L, -2, 0);
                            return (1);
                        } else {
                            lua_pushnil(L);
                            if (error) {
                                lua_pushstring(L, error);
                                if (pool) apr_pool_destroy(pool);
                                return (2);
                            }

                            if (pool) apr_pool_destroy(pool);
                            return (1);
                        }
                    }

                    if (pool) apr_pool_clear(pool);
                    if (pool) apr_pool_destroy(pool);
                    lua_pushnil(L);
                    lua_pushliteral(L, "No database connection string was specified.");
                    return (2);
                } else {
                    if (pool) apr_pool_clear(pool);
                    if (pool) apr_pool_destroy(pool);
                    lua_pushnil(thread->state);
                    lua_pushfstring(thread->state, "The database driver for '%s' could not be found!", type);
                    lua_pushinteger(thread->state, rc);
                    return (3);
                }
            }
        lua_pushnil(thread->state);
        return (1);
    }
    return 0;
}

/*
 =======================================================================================================================
    lua_header(lua_State *L): header(key, value): Sets a given HTTP header key and value.
 =======================================================================================================================
 */
static int lua_header(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *key,
                *value;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        luaL_checktype(L, 1, LUA_TSTRING);
        key = lua_tostring(L, 1);
        value = lua_tostring(L, 2);
        value = value ? value : "(nil)";
        lua_settop(L, 0);
        apr_table_set(thread->r->headers_out, key, value);
    }
    return (0);
}

/*
 =======================================================================================================================
    lua_flush(lua_State *L): flush(): Flushes the output buffer.
 =======================================================================================================================
 */
static int lua_flush(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        ap_rflush(thread->r);
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_sleep(lua_State *L): sleep(N): Sleeps for N seconds
 =======================================================================================================================
 */
static int lua_sleep(lua_State *L) {

    /*~~~~~~*/
    int n = 1;
    /*~~~~~~*/

    luaL_checktype(L, 1, LUA_TNUMBER);
    n = luaL_optint(L, 1, 1);
    sleep(((LUA_TIMEOUT > 0) && (LUA_TIMEOUT < n)) ? LUA_TIMEOUT : n);
    return (0);
}

/*
 =======================================================================================================================
    lua_echo(lua_State *L): echo(...): Same as print(...) in Lua.
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

    thread = pLua_get_thread(L);
    if (thread) {
        z = lua_gettop(L);
        for (y = 1; y < z; y++) {
            x = 0;
            if (PLUA_LSTRING) {
                el = lua_tolstring(L, y, &x);
                if (el && x > 0) {
                    ap_rwrite(el, x, thread->r);
                }
            } else {
                el = lua_tostring(L, y);
                if (el) ap_rputs(el, thread->r);
            }
        }

        lua_settop(L, 0);
    } else {
        fprintf(stderr, "Couldn't get the lua handle :(\r\n");
        fflush(stderr);

        /*
         * ap_rputs("Couldn't find our userdata :(",thread->r);
         */
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_setContentType(lua_State *L): setContentType(type): Sets the content type of the returned output.
 =======================================================================================================================
 */
static int lua_setContentType(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *el;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
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
    lua_setErrorLevel(lua_State *L): showErrors(true/false): Sets whether or not to output errors to the client.
 =======================================================================================================================
 */
static int lua_setErrorLevel(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~*/
    int         level = 0;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TBOOLEAN);
    level = lua_toboolean(L, 1);
    thread = pLua_get_thread(L);
    if (thread) {
        thread->errorLevel = level;
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_setReturnCode(lua_State *L): setReturnCode(rc): Sets the return code of the HTTP output to _rc_.
 =======================================================================================================================
 */
static int lua_setReturnCode(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    int         rc;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        rc = luaL_optint(L, 1, 0);
        lua_settop(L, 0);
        thread->r->status = rc;
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_setReturnCode(lua_State *L): setReturnCode(rc): Sets the return code of the HTTP output to _rc_.
 =======================================================================================================================
 */
static int lua_httpError(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    int         rc;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {
        rc = luaL_optint(L, 1, 0);
        lua_settop(L, 0);
        thread->returnCode = rc;
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_getEnv(lua_State *L): getEnv(): Returns a table with the current HTTP request environment.
 =======================================================================================================================
 */
static int lua_getEnv(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    lua_thread                  *thread;
    const apr_array_header_t    *fields;
    int                         i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        apr_table_entry_t   *e = 0;
        char                *pwd = getPWD(thread);
        char                luaVersion[32];
        const char *x;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        sprintf(luaVersion, "%u.%u", (LUA_VERSION_NUM / 100), (LUA_VERSION_NUM) % (LUA_VERSION_NUM / 100));
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
        lua_pushstring(thread->state, "pLua-States");
        lua_pushinteger(thread->state, LUA_STATES);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Runs");
        lua_pushinteger(thread->state, LUA_RUNS);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-UID");
        lua_pushinteger(thread->state, LUA_RUN_AS_UID);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Sessions");
        lua_pushinteger(thread->state, thread->sessions);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Files");
        lua_pushinteger(thread->state, LUA_FILES);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Memory-Limit");
        lua_pushinteger(thread->state, LUA_MEM_LIMIT);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Version");
        lua_pushinteger(thread->state, PLUA_VERSION);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Handle");
        lua_pushfstring(thread->state, "%p", thread);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Lua-State");
        lua_pushfstring(thread->state, "%p", thread->state);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Lua-Version");
        lua_pushstring(thread->state, luaVersion);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "pLua-Disabled-Libraries");
        lua_pushstring(thread->state, LUA_IGNORE);
        lua_rawset(L, -3);
        
        /* Apache HTTP specific data */
        lua_pushstring(thread->state, "Request-Time");
        lua_pushinteger(thread->state, thread->r->request_time);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Clock-Offset");
        lua_pushinteger(thread->state, pLua_clockOffset.seconds);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Remote-Address");
#if (AP_SERVER_MINORVERSION_NUMBER > 2)
        lua_pushstring(thread->state, thread->r->connection->client_ip);
#else
        lua_pushstring(thread->state, thread->r->connection->remote_ip);
#endif
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
        lua_pushstring(thread->state, "Unparsed-URI");
        lua_pushstring(thread->state, thread->r->unparsed_uri);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Request");
        lua_pushstring(thread->state, thread->r->the_request);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "Path-Info");
        lua_pushstring(thread->state, thread->r->path_info);
        lua_rawset(L, -3);
        lua_pushstring(thread->state, "ServerName");
        lua_pushstring(thread->state, thread->r->server->server_hostname);
        lua_rawset(L, -3);
        if (pwd) {
            lua_pushstring(thread->state, "Working-Directory");
            lua_pushstring(thread->state, pwd);
            lua_rawset(L, -3);
        }

        lua_pushstring(thread->state, "Server-Banner");
        lua_pushstring(thread->state, ap_get_server_banner());
        lua_rawset(L, -3);

        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
    lua_fileinfo(lua_State *L): file.stat(filename): Returns a table with information on the given file.
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
    lua_clock(lua_State *L): clock(): Returns a high definition clock value for use with benchmarking.
 =======================================================================================================================
 */
static int lua_clock(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    pLuaClock   now = pLua_getClock(0);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    lua_newtable(L);
    lua_pushliteral(L, "seconds");
    lua_pushinteger(L, now.seconds + pLua_clockOffset.seconds);
    lua_rawset(L, -3);
    lua_pushliteral(L, "nanoseconds");
    lua_pushinteger(L, now.nanoseconds + pLua_clockOffset.nanoseconds);
    lua_rawset(L, -3);
    return (1);
}

/*
 =======================================================================================================================
    lua_compileTime(lua_State *L): compileTime(): Returns the time when the request for this script got called using
    the same value type as clock().
 =======================================================================================================================
 */
static int lua_compileTime(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
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
    util_read(request_rec *r, const char **rbuf, apr_off_t *size): Reads any additional form data sent in POST
    requests.
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

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    int         z,
                i;
    size_t      x,
                y;
    const char  *key,
                *val;
    formdata    *form = apr_pcalloc(thread->r->pool, sizeof(formdata) * (MAX_VARS + 1));
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (!data || !strlen(data)) return (0);
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

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
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
    formdata    *form = (formdata *) apr_pcalloc(thread->r->pool, sizeof(formdata) * (MAX_VARS + 1));
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

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

    thread = pLua_get_thread(L);
    lua_newtable(L);
    if (thread) {
        if (thread->r->method_number != M_POST || thread->parsedPost == 1) {
            return (1);
        }

        thread->parsedPost = 1;
        memset(multipart, 0, 256);
        type = apr_table_get(thread->r->headers_in, "Content-Type");
        if (sscanf(type, "multipart/form-data; boundary=%250c", multipart) == 1) {
            if (util_read(thread->r, &data, &size) != OK) {
                return (1);
            }

            parse_multipart(thread, data, multipart, size);
            return (1);
        }

        if (strcasecmp(type, DEFAULT_ENCTYPE) == 0) {
            if (util_read(thread->r, &data, &size) != OK) {
                return (1);
            }

            parse_urlencoded(thread, data);
            return (1);
        }

        return (1);
    } else return (1);
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

    thread = pLua_get_thread(L);
    lua_settop(L, 0);
    lua_newtable(L);
    if (thread) {
        data = thread->r->args;
        parse_urlencoded(thread, data);
        return (1);
    }

    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_includeFile(lua_State *L) {

    /*~~~~~~~~~~~~~~~~~~~~~~~*/
    const char  *filename;
    lua_thread  *thread = 0;
    char        compileRaw = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~*/

    thread = pLua_get_thread(L);
    if (thread) {

        /*~~~~~~~~~~~~~~~~~*/
        apr_finfo_t fileinfo;
        int         rc = 0;
        /*~~~~~~~~~~~~~~~~~*/

        luaL_checktype(L, 1, LUA_TSTRING);
        filename = lua_tostring(L, 1);
        lua_settop(L, 0);
        apr_stat(&fileinfo, filename, APR_FINFO_NORM, thread->r->pool);
        rc = ((fileinfo.filetype != APR_NOFILE) && !(fileinfo.filetype & APR_DIR));
        if (!rc) lua_pushboolean(L, 0);
        else {
            rc = lua_compile_file(thread, filename, &fileinfo, compileRaw);
            if (rc > 0) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, rc);
                rc = lua_pcall(L, 0, LUA_MULTRET, 0);
                if (rc) {
                    pLua_print_error(thread, "Run-time error", filename);
                    lua_pushboolean(L, 0);
                } else lua_pushboolean(L, 1);
            } else {
                pLua_print_error(thread, "Compiler error in included file", filename);
                lua_pushboolean(L, 0);
            }
        }
    }

    return (1);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void register_lua_functions(lua_State *L) {
    lua_atpanic(L, module_lua_panic);
    luaL_register(L, "file", File_methods);
    luaL_register(L, "string", String_methods);
    luaL_register(L, "_G", Global_methods);
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua state functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
    Creates and initializes a new Lua state @param thread the lua_thread pointer to use @param x The index of the state
    in the grand scheme
 =======================================================================================================================
 */
void pLua_create_state(lua_thread *thread, int x) {

    /*~~~~~~~~~~~*/
    lua_State   *L;
    int         y;
    /*~~~~~~~~~~~*/

    thread->working = 0;
    thread->youngest = 0;
    thread->state = luaL_newstate();
    thread->sessions = 0;
    L = (lua_State *) thread->state;
    luaopen_base(L);
    luaopen_table(L);
    if (!memmem(LUA_IGNORE, 255, "io", 2))      luaopen_io(L);
    luaopen_string(L);
    luaopen_math(L);
    //if (!memmem(LUA_IGNORE, 255, "package", 7)) luaopen_package(L);
    //if (!memmem(LUA_IGNORE, 255, "debug", 5))   luaopen_debug(L);
    //if (!memmem(LUA_IGNORE, 255, "os", 2))      luaopen_os(L);
    //luaL_openlibs(L);
    register_lua_functions(L);

    /* Push the lua_thread struct onto the Lua registry */
    lua_pushlightuserdata(L, thread);
    lua_rawseti(L, LUA_REGISTRYINDEX, 0);
    for (y = 0; y < LUA_FILES; y++) {
        memset(thread->files[y].filename, 0, 256);
        thread->files[y].modified = 0;
        thread->files[y].size = 0;
        thread->files[y].refindex = 0;
    }
}

/*$2
 -----------------------------------------------------------------------------------------------------------------------
    Lua state pool generator
 -----------------------------------------------------------------------------------------------------------------------
 */

/*
 =======================================================================================================================
    pLua_init_states(lua_domain *domain): Creates and initialises a pool of states to be used with the given vhost.
 =======================================================================================================================
 */
void pLua_init_states(lua_domain *domain) {

    /*~~*/
    int y;
    /*~~*/

    domain->states = (lua_thread *) apr_pcalloc(domain->pool, (LUA_STATES + 1) * sizeof(lua_thread));
    y = (LUA_STATES + 1) * sizeof(lua_thread);
    if (LUA_LOGLEVEL >= 2)
    {
#if (AP_SERVER_MINORVERSION_NUMBER <= 2)
        ap_log_perror("mod_plua.c", 2456, APLOG_NOTICE, -1, domain->pool, "Allocated new domain pool for '%s' of size %u (%u states)",
                      domain->domain, y, LUA_STATES);
#else
        ap_log_perror("mod_plua.c", 2456, 1, APLOG_NOTICE, -1, domain->pool, "Allocated new domain pool for '%s' of size %u (%u states)",
                      domain->domain, y, LUA_STATES);
#endif
    }

    for (y = 0; y < LUA_STATES; y++) {
        if (!domain->states[y].state) {
            domain->states[y].files = apr_pcalloc(domain->pool, (LUA_FILES + 1) * sizeof(pLua_files));
            pLua_create_state(&domain->states[y], y);
            domain->states[y].bigPool = domain->pool;
            domain->states[y].domain = (void *) domain;
        }
    }
}

/*
 =======================================================================================================================
    Acquires a Lua state from the global stack
 =======================================================================================================================
 */
lua_thread *lua_acquire_state(request_rec *r, const char *hostname) {

    /*~~~~~~~~~~~~~~~~~~~~*/
    int         x,
                y;
    int         found = 0;
    lua_thread  *L = 0;
    lua_domain  *domain = 0;
    /*~~~~~~~~~~~~~~~~~~~~*/

    hostname = hostname ? hostname : "localDomain";

    /*
     * fprintf(stderr, "Looking for domain pool named '%s'\r\n", hostname);
     */
    if (LUA_MULTIDOMAIN > 0) {

        /* Lock the bigLock mutex in case we need to change the internal pool */
        pthread_mutex_lock(&pLua_bigLock);

        /* Look for an existing domain pool */
        for (x = 0; x < pLua_domainsAllocated; x++) {
            if (!strcmp(hostname, pLua_domains[x].domain)) {
                domain = &pLua_domains[x];

                /*
                 * fprintf(stderr, "Found match (%s) for (%s), returning handle\r\n", pLua_domains[x].domain, hostname);
                 * fflush(stderr);
                 */
                break;
            }
        }

        /* If no pool was allocated, make one! */
        if (!domain) {
            if (LUA_LOGLEVEL >= 2)
            {
#if (AP_SERVER_MINORVERSION_NUMBER <= 2)
                ap_log_perror("mod_plua.c", 2495, APLOG_NOTICE, APR_ENOPOOL, LUA_BIGPOOL,
                              "mod_pLua: Domain pool too small, reallocating space for new domain pool '%s' of size %u bytes <Not an Error>",
                              hostname, (uint32_t) sizeof(lua_domain) * (pLua_domainsAllocated + 3));
#else
                ap_log_perror("mod_plua.c", 2495, 1, APLOG_NOTICE, APR_ENOPOOL, LUA_BIGPOOL,
                              "mod_pLua: Domain pool too small, reallocating space for new domain pool '%s' of size %u bytes <Not an Error>",
                              hostname, (uint32_t) sizeof(lua_domain) * (pLua_domainsAllocated + 3));
#endif
            }

            pLua_domainsAllocated++;
            pLua_domains = (lua_domain *) realloc(pLua_domains, sizeof(lua_domain) * (pLua_domainsAllocated + 2));
            if (pLua_domains == 0) {
                if (LUA_LOGLEVEL >= 1)
                {
#if (AP_SERVER_MINORVERSION_NUMBER <= 2)
                    ap_log_perror("mod_plua.c", 2500, APLOG_CRIT, APR_ENOPOOL, LUA_BIGPOOL, "mod_pLua: Realloc failure! This is bad :(");
#else
                    ap_log_perror("mod_plua.c", 2500, 1, APLOG_CRIT, APR_ENOPOOL, LUA_BIGPOOL, "mod_pLua: Realloc failure! This is bad :(");
#endif
                }

                return (0);
            }

            domain = &pLua_domains[pLua_domainsAllocated - 1];
            domain->pool = LUA_BIGPOOL;
            strcpy(domain->domain, hostname);
            pthread_mutex_init(&domain->mutex, 0);
            pLua_init_states(domain);
        }

        /* Unlock the mutex */
        pthread_mutex_unlock(&pLua_bigLock);
    } else {
        domain = &pLua_domains[0];
    }

    pthread_mutex_lock(&domain->mutex);
    for (x = 0; x < LUA_STATES; x++) {
        if (domain->states[x].working == 0 && domain->states[x].state) {

            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
            apr_os_thread_t me = apr_os_thread_current();
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

            domain->states[x].working = 1;
            domain->states[x].sessions++;
            L = &domain->states[x];
            found = 1;

            /* Add the ID of the current thread to the state backup list */
            for (y = 0; y < LUA_STATES; y++) {
                if (pLua_threads[y].thread == 0) {
                    pLua_threads[y].thread = me;
                    pLua_threads[y].state = L;
                    break;
                }
            }
            break;
        }
    }

    pthread_mutex_unlock(&domain->mutex);
    if (found)
    {
        /*~~~~~~~~~~~~~~~~~~~~~~*/
#ifdef _WIN32
        LARGE_INTEGER   cycles;
        LARGE_INTEGER   frequency;
        /*~~~~~~~~~~~~~~~~~~~~~~*/

        QueryPerformanceCounter(&cycles);
        QueryPerformanceFrequency(&frequency);
        L->t.tv_sec = (cycles.QuadPart / frequency.QuadPart) + pLua_clockOffset.seconds;
        L->t.tv_nsec = (cycles.QuadPart % frequency.QuadPart) * (1000000000 / frequency.QuadPart) + pLua_clockOffset.nanoseconds;
#else
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        apr_time_t  now = r->request_time;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        L->t.tv_sec = (now / 1000000);
        L->t.tv_nsec = ((now % 1000000) * 1000);
#endif

        /* Push the lua_thread struct onto the Lua registry */
        lua_pushlightuserdata(L->state, L);
        lua_rawseti(L->state, LUA_REGISTRYINDEX, 0);
        return (L);
    } else {
        sleep(1);
        return (lua_acquire_state(r, hostname));
    }
}

/*
 =======================================================================================================================
    Releases a Lua state and frees it up for use elsewhere @param X the lua_State to release
 =======================================================================================================================
 */
void lua_release_state(lua_thread *thread) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    int             y;
    apr_os_thread_t me = apr_os_thread_current();
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (thread->sessions % 5) lua_gc(thread->state, LUA_GCSTEP, 1);

    /* Check if state needs restarting */
    if (thread->sessions > LUA_RUNS) {
        lua_close(thread->state);
        pLua_create_state(thread, 1);
    }

    /* Remove the thread from the backup state list */
    for (y = 0; y < LUA_STATES; y++) {
        if (pLua_threads[y].thread == me) {
            pLua_threads[y].thread = 0;
            pLua_threads[y].state = 0;
            break;
        }
    }

    /* Signal that the state is ready for use again */
    thread->working = 0;
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Cryptographic functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void sha256_starts(sha256_context *ctx) {
    ctx->total[0] = 0;
    ctx->total[1] = 0;
    ctx->state[0] = 0x6A09E667;
    ctx->state[1] = 0xBB67AE85;
    ctx->state[2] = 0x3C6EF372;
    ctx->state[3] = 0xA54FF53A;
    ctx->state[4] = 0x510E527F;
    ctx->state[5] = 0x9B05688C;
    ctx->state[6] = 0x1F83D9AB;
    ctx->state[7] = 0x5BE0CD19;
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void sha256_process(sha256_context *ctx, uint8_t data[64]) {

    /*~~~~~~~~~~~~~~*/
    uint32_t    temp1,
                temp2,
                W[64];
    uint32_t    A,
                B,
                C,
                D,
                E,
                F,
                G,
                H;
    /*~~~~~~~~~~~~~~*/

    GET_UINT32(W[0], data, 0);
    GET_UINT32(W[1], data, 4);
    GET_UINT32(W[2], data, 8);
    GET_UINT32(W[3], data, 12);
    GET_UINT32(W[4], data, 16);
    GET_UINT32(W[5], data, 20);
    GET_UINT32(W[6], data, 24);
    GET_UINT32(W[7], data, 28);
    GET_UINT32(W[8], data, 32);
    GET_UINT32(W[9], data, 36);
    GET_UINT32(W[10], data, 40);
    GET_UINT32(W[11], data, 44);
    GET_UINT32(W[12], data, 48);
    GET_UINT32(W[13], data, 52);
    GET_UINT32(W[14], data, 56);
    GET_UINT32(W[15], data, 60);
#define SHR(x, n)   ((x & 0xFFFFFFFF) >> n)
#define ROTR(x, n)  (SHR(x, n) | (x << (32 - n)))
#define S0(x)       (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define S1(x)       (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))
#define S2(x)       (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x)       (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define F0(x, y, z) ((x & y) | (z & (x | y)))
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define R(t)        (W[t] = S1(W[t - 2]) + W[t - 7] + S0(W[t - 15]) + W[t - 16])
#define P(a, b, c, d, e, f, g, h, x, K) { \
        temp1 = h + S3(e) + F1(e, f, g) + K + x; \
        temp2 = S2(a) + F0(a, b, c); \
        d += temp1; \
        h = temp1 + temp2; \
    }

    A = ctx->state[0];
    B = ctx->state[1];
    C = ctx->state[2];
    D = ctx->state[3];
    E = ctx->state[4];
    F = ctx->state[5];
    G = ctx->state[6];
    H = ctx->state[7];
    P(A, B, C, D, E, F, G, H, W[0], 0x428A2F98);
    P(H, A, B, C, D, E, F, G, W[1], 0x71374491);
    P(G, H, A, B, C, D, E, F, W[2], 0xB5C0FBCF);
    P(F, G, H, A, B, C, D, E, W[3], 0xE9B5DBA5);
    P(E, F, G, H, A, B, C, D, W[4], 0x3956C25B);
    P(D, E, F, G, H, A, B, C, W[5], 0x59F111F1);
    P(C, D, E, F, G, H, A, B, W[6], 0x923F82A4);
    P(B, C, D, E, F, G, H, A, W[7], 0xAB1C5ED5);
    P(A, B, C, D, E, F, G, H, W[8], 0xD807AA98);
    P(H, A, B, C, D, E, F, G, W[9], 0x12835B01);
    P(G, H, A, B, C, D, E, F, W[10], 0x243185BE);
    P(F, G, H, A, B, C, D, E, W[11], 0x550C7DC3);
    P(E, F, G, H, A, B, C, D, W[12], 0x72BE5D74);
    P(D, E, F, G, H, A, B, C, W[13], 0x80DEB1FE);
    P(C, D, E, F, G, H, A, B, W[14], 0x9BDC06A7);
    P(B, C, D, E, F, G, H, A, W[15], 0xC19BF174);
    P(A, B, C, D, E, F, G, H, R(16), 0xE49B69C1);
    P(H, A, B, C, D, E, F, G, R(17), 0xEFBE4786);
    P(G, H, A, B, C, D, E, F, R(18), 0x0FC19DC6);
    P(F, G, H, A, B, C, D, E, R(19), 0x240CA1CC);
    P(E, F, G, H, A, B, C, D, R(20), 0x2DE92C6F);
    P(D, E, F, G, H, A, B, C, R(21), 0x4A7484AA);
    P(C, D, E, F, G, H, A, B, R(22), 0x5CB0A9DC);
    P(B, C, D, E, F, G, H, A, R(23), 0x76F988DA);
    P(A, B, C, D, E, F, G, H, R(24), 0x983E5152);
    P(H, A, B, C, D, E, F, G, R(25), 0xA831C66D);
    P(G, H, A, B, C, D, E, F, R(26), 0xB00327C8);
    P(F, G, H, A, B, C, D, E, R(27), 0xBF597FC7);
    P(E, F, G, H, A, B, C, D, R(28), 0xC6E00BF3);
    P(D, E, F, G, H, A, B, C, R(29), 0xD5A79147);
    P(C, D, E, F, G, H, A, B, R(30), 0x06CA6351);
    P(B, C, D, E, F, G, H, A, R(31), 0x14292967);
    P(A, B, C, D, E, F, G, H, R(32), 0x27B70A85);
    P(H, A, B, C, D, E, F, G, R(33), 0x2E1B2138);
    P(G, H, A, B, C, D, E, F, R(34), 0x4D2C6DFC);
    P(F, G, H, A, B, C, D, E, R(35), 0x53380D13);
    P(E, F, G, H, A, B, C, D, R(36), 0x650A7354);
    P(D, E, F, G, H, A, B, C, R(37), 0x766A0ABB);
    P(C, D, E, F, G, H, A, B, R(38), 0x81C2C92E);
    P(B, C, D, E, F, G, H, A, R(39), 0x92722C85);
    P(A, B, C, D, E, F, G, H, R(40), 0xA2BFE8A1);
    P(H, A, B, C, D, E, F, G, R(41), 0xA81A664B);
    P(G, H, A, B, C, D, E, F, R(42), 0xC24B8B70);
    P(F, G, H, A, B, C, D, E, R(43), 0xC76C51A3);
    P(E, F, G, H, A, B, C, D, R(44), 0xD192E819);
    P(D, E, F, G, H, A, B, C, R(45), 0xD6990624);
    P(C, D, E, F, G, H, A, B, R(46), 0xF40E3585);
    P(B, C, D, E, F, G, H, A, R(47), 0x106AA070);
    P(A, B, C, D, E, F, G, H, R(48), 0x19A4C116);
    P(H, A, B, C, D, E, F, G, R(49), 0x1E376C08);
    P(G, H, A, B, C, D, E, F, R(50), 0x2748774C);
    P(F, G, H, A, B, C, D, E, R(51), 0x34B0BCB5);
    P(E, F, G, H, A, B, C, D, R(52), 0x391C0CB3);
    P(D, E, F, G, H, A, B, C, R(53), 0x4ED8AA4A);
    P(C, D, E, F, G, H, A, B, R(54), 0x5B9CCA4F);
    P(B, C, D, E, F, G, H, A, R(55), 0x682E6FF3);
    P(A, B, C, D, E, F, G, H, R(56), 0x748F82EE);
    P(H, A, B, C, D, E, F, G, R(57), 0x78A5636F);
    P(G, H, A, B, C, D, E, F, R(58), 0x84C87814);
    P(F, G, H, A, B, C, D, E, R(59), 0x8CC70208);
    P(E, F, G, H, A, B, C, D, R(60), 0x90BEFFFA);
    P(D, E, F, G, H, A, B, C, R(61), 0xA4506CEB);
    P(C, D, E, F, G, H, A, B, R(62), 0xBEF9A3F7);
    P(B, C, D, E, F, G, H, A, R(63), 0xC67178F2);
    ctx->state[0] += A;
    ctx->state[1] += B;
    ctx->state[2] += C;
    ctx->state[3] += D;
    ctx->state[4] += E;
    ctx->state[5] += F;
    ctx->state[6] += G;
    ctx->state[7] += H;
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void sha256_update(sha256_context *ctx, uint8_t *input, uint32_t length) {

    /*~~~~~~~~~~~~~*/
    uint32_t    left,
                fill;
    /*~~~~~~~~~~~~~*/

    if (!length) return;
    left = ctx->total[0] & 0x3F;
    fill = 64 - left;
    ctx->total[0] += length;
    ctx->total[0] &= 0xFFFFFFFF;
    if (ctx->total[0] < length) ctx->total[1]++;
    if (left && length >= fill) {
        memcpy((void *) (ctx->buffer + left), (void *) input, fill);
        sha256_process(ctx, ctx->buffer);
        length -= fill;
        input += fill;
        left = 0;
    }

    while (length >= 64) {
        sha256_process(ctx, input);
        length -= 64;
        input += 64;
    }

    if (length) {
        memcpy((void *) (ctx->buffer + left), (void *) input, length);
    }
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
void sha256_finish(sha256_context *ctx, uint8_t digest[32]) {

    /*~~~~~~~~~~~~~~~~~~*/
    uint32_t    last,
                padn;
    uint32_t    high,
                low;
    uint8_t     msglen[8];
    /*~~~~~~~~~~~~~~~~~~*/

    high = (ctx->total[0] >> 29) | (ctx->total[1] << 3);
    low = (ctx->total[0] << 3);
    PUT_UINT32(high, msglen, 0);
    PUT_UINT32(low, msglen, 4);
    last = ctx->total[0] & 0x3F;
    padn = (last < 56) ? (56 - last) : (120 - last);
    sha256_update(ctx, sha256_padding, padn);
    sha256_update(ctx, msglen, 8);
    PUT_UINT32(ctx->state[0], digest, 0);
    PUT_UINT32(ctx->state[1], digest, 4);
    PUT_UINT32(ctx->state[2], digest, 8);
    PUT_UINT32(ctx->state[3], digest, 12);
    PUT_UINT32(ctx->state[4], digest, 16);
    PUT_UINT32(ctx->state[5], digest, 20);
    PUT_UINT32(ctx->state[6], digest, 24);
    PUT_UINT32(ctx->state[7], digest, 28);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
char *pLua_sha256(const char *digest, lua_thread *thread) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    sha256_context  ctx;
    char            *ret = (char *) apr_palloc(thread->r->pool, 72);
    unsigned char   shasum[33];
    unsigned char   Rshasum[33];
    uint32_t        *shaX = 0;
    int             x = 0;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    sha256_starts(&ctx);
    sha256_update(&ctx, (uint8_t *) digest, strlen(digest));
    sha256_finish(&ctx, shasum);
    for (x = 0; x < 32; x += 4) {
        Rshasum[x] = shasum[x + 3];
        Rshasum[x + 1] = shasum[x + 2];
        Rshasum[x + 2] = shasum[x + 1];
        Rshasum[x + 3] = shasum[x];
    }

    shaX = (uint32_t *) Rshasum;
    sprintf(ret, "%08x%08x%08x%08x%08x%08x%08x%08x", shaX[0], shaX[1], shaX[2], shaX[3], shaX[4], shaX[5], shaX[6], shaX[7]);
    return (ret);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static char value(char c) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    const char  *p = strchr(b64_table, c);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (p) {
        return (p - b64_table);
    } else {
        return (0);
    }
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
int pLua_unbase64(unsigned char *dest, const unsigned char *src, size_t srclen) {

    /*~~~~~~~~~~~~~~~~~~~~~~*/
    unsigned char   *p = dest;
    /*~~~~~~~~~~~~~~~~~~~~~~*/

    *dest = 0;
    if (*src == 0) {
        return (0);
    }

    do
    {
        /*~~~~~~~~~~~~~~~~~~~~~~*/
        char    a = value(src[0]);
        char    b = value(src[1]);
        char    c = value(src[2]);
        char    d = value(src[3]);
        /*~~~~~~~~~~~~~~~~~~~~~~*/

        *p++ = (a << 2) | (b >> 4);
        *p++ = (b << 4) | (c >> 2);
        *p++ = (c << 6) | d;
        if (!isbase64(src[1])) {
            p -= 2;
            break;
        } else if (!isbase64(src[2])) {
            p -= 2;
            break;
        } else if (!isbase64(src[3])) {
            p--;
            break;
        }

        src += 4;
        while (*src && (*src == 13 || *src == 10)) src++;
    } while (srclen -= 4);
    *p = 0;
    return (p - dest);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
char *pLua_decode_base64(const char *src, lua_thread *thread) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    size_t  ilen = strlen(src);
    char    *output = (char *) apr_pcalloc(thread->r->pool, ilen + 2);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    pLua_unbase64((unsigned char *) output, (const unsigned char *) src, ilen);
    return (output);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
char base64_encode_value(char value_in) {
    if (value_in > 63) return ('=');
    return (b64_table[(int) value_in]);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
int base64_encode_block(const char *plaintext_in, size_t length_in, char *code_out, base64_encodestate *state_in) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    const char          *plainchar = plaintext_in;
    const char *const   plaintextend = plaintext_in + length_in;
    char                *codechar = code_out;
    char                result;
    char                fragment;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    result = state_in->result;
    switch (state_in->step) {
        while (1)
        {
        case 1:
            if (plainchar == plaintextend) {
                state_in->result = result;
                state_in->step = 1;
                return (codechar - code_out);
            }

            fragment = *plainchar++;
            result = (fragment & 0x0fc) >> 2;
            *codechar++ = b64enc(result);
            result = (fragment & 0x003) << 4;

        case 2:
            if (plainchar == plaintextend) {
                state_in->result = result;
                state_in->step = 2;
                return (codechar - code_out);
            }

            fragment = *plainchar++;
            result |= (fragment & 0x0f0) >> 4;
            *codechar++ = b64enc(result);
            result = (fragment & 0x00f) << 2;

        case 3:
            if (plainchar == plaintextend) {
                state_in->result = result;
                state_in->step = 3;
                return (codechar - code_out);
            }

            fragment = *plainchar++;
            result |= (fragment & 0x0c0) >> 6;
            *codechar++ = b64enc(result);
            result = (fragment & 0x03f) >> 0;
            *codechar++ = b64enc(result);
            ++(state_in->stepcount);
            if (state_in->stepcount == BASE64_CHARS_PER_LINE / 4) {
                *codechar++ = '\n';
                state_in->stepcount = 0;
            }
        }
    }

    /* control should not reach here */
    return (codechar - code_out);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
int base64_encode_blockend(char *code_out, base64_encodestate *state_in) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~*/
    char    *codechar = code_out;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~*/

    switch (state_in->step)
    {
    case 2: *codechar++ = b64enc(state_in->result); *codechar++ = '='; *codechar++ = '='; break;
    case 3: *codechar++ = b64enc(state_in->result); *codechar++ = '='; break;
    case 1: break;
    }

    *codechar++ = '\n';
    return (codechar - code_out);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
char *pLua_encode_base64(const char *src, size_t len, lua_thread *thread) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    base64_encodestate  state;
    char                *output;
    int                 n;
    size_t              olen = (len * (4 / 3)) + 1024;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    output = apr_pcalloc(thread->r->pool, olen);
    state.step = 1;
    state.result = 0;
    state.stepcount = 0;
    n = base64_encode_block(src, len, output, &state);
    if (n) n = base64_encode_blockend((char *) output + n, &state);
    return (output);
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua Lua functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_sha256(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *string;
    char        *output;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    string = lua_tostring(L, 1);
    thread = pLua_get_thread(L);
    if (thread) {
        output = pLua_sha256((const char *) string, thread);
        lua_settop(L, 0);
        lua_pushstring(L, output);
        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_b64dec(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *string;
    char        *output;
    size_t      ilen,
                olen;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    string = lua_tostring(L, 1);
    ilen = strlen(string);
    thread = pLua_get_thread(L);
    if (thread) {
        if (ilen) {
            output = apr_pcalloc(thread->r->pool, ilen + 1);
            olen = pLua_unbase64((unsigned char *) output, (const unsigned char *) string, ilen);
            lua_settop(L, 0);
            lua_pushlstring(L, output, olen);
        } else {
            lua_settop(L, 0);
            lua_pushliteral(L, "");
        }

        return (1);
    }

    return (0);
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static int lua_b64enc(lua_State *L) {

    /*~~~~~~~~~~~~~~~~*/
    const char  *string;
    char        *output;
    size_t      ilen;
    lua_thread  *thread;
    /*~~~~~~~~~~~~~~~~*/

    luaL_checktype(L, 1, LUA_TSTRING);
    string = lua_tostring(L, 1);
    ilen = strlen(string);
    thread = pLua_get_thread(L);
    if (thread) {
        if (ilen) {
            output = pLua_encode_base64(string, ilen, thread);
            lua_settop(L, 0);
            lua_pushstring(L, output);
        } else {
            lua_settop(L, 0);
            lua_pushliteral(L, "");
        }

        return (1);
    }

    return (0);
}

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Configuration directives
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 =======================================================================================================================
    pLuaStates N Sets the available amount of Lua states to N states. Default is 50
 =======================================================================================================================
 */
const char *pLua_set_LuaStates(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_STATES = x ? x : 50;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaRuns N Restarts Lua States after N sessions. Default is 500
 =======================================================================================================================
 */
const char *pLua_set_LuaRuns(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_RUNS = x ? x : 500;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaFiles N: Sets the file cache array to hold N elements. Default is 200. Each 100 elements take up 30kb of
    memory, so having 200 elements in 50 states will use 3MB of memory. If you run a large server with many scripts and
    domains, you may want to set this to a higher number, fx. 1000.
 =======================================================================================================================
 */
const char *pLua_set_LuaFiles(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_FILES = x ? x : 200;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaTimeout N: Sets the timeout to N seconds for any pLua scripts run by the handler. Default is 0 (disabled).
 =======================================================================================================================
 */
const char *pLua_set_Timeout(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_TIMEOUT = x > 0 ? x : 0;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaError N: Sets whether or not to output error messages to the client. Set to 1 for enabled, 0 for disabled.
 =======================================================================================================================
 */
const char *pLua_set_Logging(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_PERROR = x > 0 ? x : 0;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaMultiDomain N: Sets whether or not to enable multidomain support. Set to 1 for enabled, 0 for disabled.
 =======================================================================================================================
 */
const char *pLua_set_Multi(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_MULTIDOMAIN = x > 0 ? x : 0;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaLogLevel N: Sets the logging level for pLua 0 = disable all 1 = errors 2 = module notices 3 = everything
    including script errors
 =======================================================================================================================
 */
const char *pLua_set_LogLevel(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_LOGLEVEL = x > 0 ? x : 0;
    return (NULL);
}

/*
 =======================================================================================================================
    pLuaRaw <ext>: Sets the handler to treat files with the extension <ext> as plain Lua files. example: pLuaRaw .lua
 =======================================================================================================================
 */
const char *pLua_set_Raw(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~*/
    int x = 0;
    /*~~~~~~*/

    for (x = 0; x < PLUA_RAW_TYPES; x++) {
        if (!strlen(pLua_rawTypes[x])) {
            strcpy(pLua_rawTypes[x], arg);
            break;
        }
    }

    return (NULL);
}

/*
 =======================================================================================================================
    pLuaShortHand N: Sets whether or not to enable shorthand opening tag support. Set to 1 for enabled, 0 for disabled.
 =======================================================================================================================
 */
const char *pLua_set_ShortHand(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_SHORTHAND = x > 0 ? x : 0;
    return (NULL);
}



/*
 =======================================================================================================================
    pLuaMemoryLimit N: Sets the memory limit (in kb) for each state.
 =======================================================================================================================
 */
const char *pLua_set_MemoryLimit(cmd_parms *cmd, void *cfg, const char *arg) {

    /*~~~~~~~~~~~~~~*/
    int x = atoi(arg);
    /*~~~~~~~~~~~~~~*/

    LUA_MEM_LIMIT = x > 0 ? x : 0;
    return (NULL);
}



/*
 =======================================================================================================================
    pLuaIgnoreLibrary lib: Ignores one or more libraries from being loaded into the Lua state.
 *  This is useful for sandboxing Lua.
 =======================================================================================================================
 */
const char *pLua_set_Ignore(cmd_parms *cmd, void *cfg, const char *arg) {
    sprintf(LUA_IGNORE, arg);
    return (NULL);
}
