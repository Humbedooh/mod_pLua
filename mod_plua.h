/*$I0 */
#ifndef _MOD_PLUA_H_    /* mod_plua.h */

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua definitions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#   define LINUX   2
#   define _LARGEFILE64_SOURCE
#   define LUA_COMPAT_MODULE   1
#   define PLUA_VERSION        48
#   define DEFAULT_ENCTYPE     "application/x-www-form-urlencoded"
#   define MULTIPART_ENCTYPE   "multipart/form-data"
#   define MAX_VARS            500  /* Maximum number of HTTP GET/POST variables */
#   define MAX_MULTIPLES       50   /* Maximum number of chained GET/POST variables */
#   define PLUA_DEBUG          0
#   define PLUA_LSTRING        1    /* Use tolstring(binary compatible) or just tostring? */
#   ifdef _WIN32
#      define sleep(a)    Sleep(a * 1000)
#   endif

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua includes
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#   include <stdio.h>
#   include <stdlib.h>
#   include <sys/stat.h>
#   include <time.h>
#   include <apr_dbd.h>
#   include <apr_hash.h>
#   include <httpd.h>
#   include <http_protocol.h>
#   include <http_config.h>
#   include <http_log.h>
#   ifndef _WIN32
#      include <unistd.h>
#      include <pthread.h>
#   endif
#   include <lua.h>
#   include <lualib.h>
#   include <lauxlib.h>
#   include <time.h>
#   include <stdint.h>
#   ifndef R_OK
#      define R_OK    0x04  /* test for read permission */
#   endif
#   ifdef _WIN32
typedef HANDLE  pthread_mutex_t;
#      ifndef HAVE_STRUCT_TIMESPEC
#         define HAVE_STRUCT_TIMESPEC
#         ifndef _TIMESPEC_DEFINED
#            define _TIMESPEC_DEFINED
struct timespec
{
    time_t  tv_sec;
    long    tv_nsec;
};
#         endif /* _TIMESPEC_DEFINED */
#      endif /* HAVE_STRUCT_TIMESPEC */
#   endif

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mod_pLua structs and globals
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

static int              LUA_STATES = 50;        /* Keep 50 states open */
static int              LUA_RUNS = 1000;        /* Restart a state after 1000 sessions */
static int              LUA_FILES = 50;         /* Number of files to keep cached */
static int              LUA_TIMEOUT = 0;        /* Maximum number of seconds a lua script may take (set to 0 to disable) */
static int              LUA_PERROR = 1;
static int              LUA_LOGLEVEL = 1;       /* 0: Disable, 1: Log errors, 2: Log warnings, 3: Log everything, including script errors */
static int              LUA_MULTIDOMAIN = 1;    /* Enable/disable multidomain support (experimental) */
static int              LUA_SHORTHAND = 1;      /* Enable/disable shorthand opening tags <? ?> */
static apr_pool_t       *LUA_BIGPOOL = 0;
static int              LUA_RUN_AS_UID;
static int              LUA_RUN_AS_GID = -1;
static int              LUA_MEM_LIMIT = 0;
static char             LUA_IGNORE[256];        /* Base libraries to ignore */
static pthread_mutex_t  pLua_bigLock;
static int              pLua_domainsAllocated = 1;
static uint32_t         then = 0;
typedef struct
{
    char        filename[257];
    time_t      modified;
    apr_off_t   size;
    int         refindex;
} pLua_files;
typedef struct
{
    int             working;
    int             sessions;
    request_rec     *r;
    apr_pool_t      *bigPool;
    lua_State       *state;
    int             typeSet;
    int             returnCode;
    int             youngest;
    char            debugging;
    char            parsedPost;
    int             errorLevel;
    struct timespec t;
    time_t          runTime;
    pLua_files      *files;
    void            *domain;
} lua_thread;
typedef struct
{
    lua_thread      *states;
    pthread_mutex_t mutex;
    char            domain[512];
    apr_pool_t      *pool;
} lua_domain;
typedef struct
{
    lua_thread      *state;
    apr_os_thread_t thread;
} lua_threadStates;
typedef struct
{
    const char  *key;
    int         size;
    int         sizes[MAX_MULTIPLES];
    const char  *values[MAX_MULTIPLES];
} formdata;
static lua_domain       *pLua_domains = 0;
static lua_threadStates *pLua_threads = 0;
typedef struct
{
    apr_dbd_t               *handle;
    const apr_dbd_driver_t  *driver;
    int                     alive;
    apr_pool_t              *pool;
    char                    type;
} dbStruct;
typedef struct
{
    apr_dbd_t           *handle;
    apr_dbd_driver_t    *driver;
    apr_hash_t          *prepared;
} ap_dbd_t;
typedef union
{
    struct
    {
        uint32_t    seconds;
        uint32_t    nanoseconds;
    };
    uint64_t    quadPart;
} pLuaClock;
static pLuaClock    pLua_clockOffset;
#   ifndef PRIx32
#      define PRIx32  "x"
#   endif
typedef struct
{
    uint32_t    total[2];
    uint32_t    state[8];
    uint8_t     buffer[64];
} sha256_context;
#   define GET_UINT32(n, b, i) { \
        (n) = ((uint32_t) (b)[(i)] << 24) | ((uint32_t) (b)[(i) + 1] << 16) | ((uint32_t) (b)[(i) + 2] << 8) | ((uint32_t) (b)[(i) + 3]); \
    }
#   define PUT_UINT32(n, b, i) { \
        (b)[(i)] = (uint8_t) ((n) >> 24); \
        (b)[(i) + 1] = (uint8_t) ((n) >> 16); \
        (b)[(i) + 2] = (uint8_t) ((n) >> 8); \
        (b)[(i) + 3] = (uint8_t) ((n)); \
    }
static const char   b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const int    BASE64_INPUT_SIZE = 57;
static const int    BASE64_CHARS_PER_LINE = 72;
#   define isbase64(c) (c && strchr(b64_table, c))
#   define b64enc(v)   (v > 63) ? '=' : b64_table[(int) v]
typedef struct
{
    char    step;
    char    result;
    int     stepcount;
} base64_encodestate;
static const char   *pLua_error_template = "<h1><img alt=\"\" src=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAACLZJREFUeNrsmwtUFNcZx7+Z2dkHy/IIIIhRCCj1GZGKxIBJ6kkTbW30NGmrJzWn1TQ92jxqTrStmsb0pLGJiebYxNNgmpoYOZgI9YWmiUqVh6Io4AtdFcuiwrIsD9lddvYx0+/O7gom7IM1DOpyOb8zw507c+/973e/+907u5QgCBDKiRoUYFCAQQGCunHJmm3P4WE+okL4Pt4uQy4h9QiHXEUakDpEh3QG+qB1S5+6JQFkwdz00ppC7Dz94S2KP6mXPPJptCCnkHJkL1KJ2PrLAoISQKCZ6f1lkUgcMt3NSkSLbEQ2ucUZeAF4ipFymKYha5A/Im8jf0e6BlYAWlIBPCkGeQt5BlmAHB1AAWQwgGkccgghTvjTUBgCvSUF8ol79vnsdrQAO3LCfaR8+VMkys0QRE71uBBAyn32g0PnPvrdQ5W3mwW0IY8jHd4K/PeTYrXBYEmlGSo6aVRC/eRZGbzbvB9D5qEQ8QEIQWKQDQs2lGV/vDjbLqEAdFBql316QHm9k3sa59HfWCy2sWaLTUN6qD15xd5wQa+zO5y7w8PluTMWz1iFxZchSykKWD+xWibyArJWskhw7vsV+Xj4hY8ize7p64YFdJZqs/R602bO5hzVPeV/czQIYr5azb6T/ET6UvznhyhAIQVUOO+7nZeRCfnPZ5klsQAZQ/WpfPOB2l82Npo+4nlBcXOHuxN2Gh59OBXkcobb+7V2UeNXp5Pi4zXz5BnJz2Jt+Qwq4UOD+5Ac5D+SCMAygQ+B6xUXH/mfrn2zd18owNCECMj78EkYhkfi4f/wYo5izjN5P7hwyViQnpkyB/OmYv9f8mOss3+dW1nyr+cmW/rSl6AGs1xGB4S5uk6lvWj8J+dwgnfssGhhpqfzLs+mZGHdmz+Kbe0wzW7Yf+ZJfNZaOUN3Klif9WUhaZJYAGlIIMlgtMwztJlSfOvMQ0pS1Ldy42PVYEO/jtbzhqZCO254zuiDaEOzfBjB95DJSHW/C6BkA/MBxlbzIs7u9LNadkJNrR6mTLz3ptyL9a1gtdvA2u4YHR2hnI51VmH2LB8PUiNjJbEAJXtzHMDzfD1N00PJ6PDknT9+OVLfYhrPOXhg0GnSOIjtDr5XH7DyrX2QeX8ipI8d6nKaRjMsWbUXOLE8D1ar41dg54qUYWE+28VZrfK+9oUOdgh4QC20RXl5sq7O9iM98w3660ONHZzS5uRhek4qvPvaDCDn30YAY4cVpvxkIzw2fzM8sTAPUrLXwdGaq+I1UsZqc4zbk59v7jDoK3rW0ZOm+stlX23bFi2JAEp0Om5MB4v2cnabbdi+HbsnyQT+nOea2WxXOnkenLxApjaIjlSJ5x5i7wmDMBUrnicM0YATXfy+0jpxsDz8IM5qNHWjrMXqiCSbIvt3FWU5rZayHvWL2C3msvL9xdnYDkESAdDbUsTz1hw7caLxmn6CgI+x2RyaPf/epcB8EyJTq+VdNFk2Y9QogKsz5NzDqy8+Ao9OGymev/faTEhNioEvt8yHnMwkWDg3A9b+eaZr5qRIYKQwYx0OUs+OrdtHM5RwweP9yfnOz7ePJ9eQS5IIwMooVZux5XDlsaqHBBKguDG0tN5XXnK4Fq9zKaOGNGmi1FZgWKzFJYR47oHGBpM1BTnHe8nx5b/uh6KDdXBFb4aYaPRpjFwk+p5wLT4/ntzTxXExOwt3O7AOC2LaUbALh4gtEq8LiE4SJ3j65LnzZWXHZgq9rAmqq89ibE4V5ORkdpYc1J01WVszGBkLxAAYebePojGYcoh5LMhkMhgxPBqWL5oKxRU6MLRxoMB8Uh5HAqSNjNtSVkN/nwgl4F9jU8uY8tJjJeh8ZU1641RwtYOE3zWSCLDvQHmaeK+XRVFVTW0W0h4eOXG9jDVtohkGxoyMhadmjBHD6Lqr7SBTsPCzWWNBExUGUzOGQVFJPQxP0MDX5TpY/PQkSEmMABZFiIpUnf24sKAQ61rhEYCko5WnprmWFDfacBKplWZTlKL87cmJ18OjwvKSk2L+cvhU84h/bD0N8XHhYnvbzQ5YvfE4rHr+AXggfRj89vViKK28CgqVHH6/YAocONwAHWY7KFUqPi4h8hXOSv2coqgMQey+1xjk0Pkdb/Z5MRTUajBtzkp/q8EL7shMSEzMTj9zuqlKdII3NV7osSCi3PA9/neiYBG5TuHUMvzgq1GAZJ4XvEdTABO12984I81+AAS+JXbl2pHqCfc/OFuna883me0q3xtE3eLExqrWW7mTS9BL7GRoOtnh9PnupfTi9tfPBNOX4HY36b5NHrprR3Ymp07LaGk2FbYYu8Z43w/AGYalreEaxYoua9UHWM9nDEP/WPzkfdf5tsRbYn2fPet0ZedIrC5PnPxTOfAvtHbYJpq67BrsG4kprBFqtt7BCwXKKFWu2Xh0OHa4BMPrTMxzSUN5tZyiuoJX90gqgEBR/keJt+2868cLOYBCgRlPJyRoYmiaYskeorO9EucFSHe2wrsUTROnh9Gh2+y910feK748ANvifi2A7PCu8VUgTDgLzk6IQ+9F4vd7KZoZgZ2OFT09SudyzrQ/l7Gs/ovlWskFAP9OkHTqFf9zkOdAdc8LYu8DasQGZP2tvmCQdFc4AOfvUsX/RPE35E/fRZX95QP6K7W5t8C3fFcPDFIAeiA6vxVZAa4vVsDACgCSClDsnue/7I+H345DgKwjziNkbi8A1zvGfkv9JQCJzVv9uDsDuF6ekmMj2Qd1L2fJ5meDVOYVlABddr8CkFdV6eB7QrPAbZCCEiCApQDpuBnugBSUAExgPoCCgF/z32kC0BTcLSk4AZhQFyDULUBG0yEuAAOhLkCIWwAb6k5QFuoCsCE/Cwz6gEELCPVACEJ9CIS4BQyuBRjK333MXe4DKH97dmSfT7hrBcDF4Go8kNfcWb08g3xRacVdbQF5y+c2z1+9dRO4fvn5zW9nktfglXeKACH/2+H/CzAARydTywOiK4YAAAAASUVORK5CYII=\" />%s:</h1><i>in %s</i><br/><pre>%s</pre>";
typedef struct
{
    const char  *sTag;
    const char  *eTag;
} pLua_tags;
static const pLua_tags  pLua_file_tags[] = { { "<?lua", "?>" }, { "<?", "?>" }, { 0, 0 } };
#   define PLUA_RAW_TYPES  32
static char             *pLua_rawTypes[PLUA_RAW_TYPES];
#   ifndef luaL_reg
#      define luaL_reg    luaL_Reg
#   endif
static uint8_t          sha256_padding[64] =
{
    0x80,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};

/*$1
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Windows specific mutex handling
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#   ifdef _WIN32

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void pthread_mutex_lock(HANDLE mutex) {

    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    DWORD   dwWaitResult = WaitForSingleObject(mutex, INFINITE);
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    return;
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void pthread_mutex_unlock(HANDLE mutex) {
    ReleaseMutex(mutex);
    return;
}

/*
 =======================================================================================================================
 =======================================================================================================================
 */
static void pthread_mutex_init(HANDLE *mutex, int blargh) {
    *mutex = CreateMutex(NULL, /* default security attributes */ FALSE, /* initially not owned */ NULL);    /* unnamed mutex */
    return;
}
#   endif
static void         pLua_print_error(lua_thread *thread, const char *type, const char *filename);
static int          module_lua_panic(lua_State *L);
static lua_thread   *pLua_get_thread(lua_State *L);
static void         pLua_debug_hook(lua_State *L, lua_Debug *ar);
void                lua_add_code(char **buffer, const char *string);
int                 lua_parse_file(lua_thread *thread, char *input);
int                 lua_compile_file(lua_thread *thread, const char *filename, apr_finfo_t *statbuf, char rawCompile);
char                *getPWD(lua_thread *thread);
void                sha256_starts(sha256_context *ctx);
void                sha256_process(sha256_context *ctx, uint8_t data[64]);
void                sha256_update(sha256_context *ctx, uint8_t *input, uint32_t length);
void                sha256_finish(sha256_context *ctx, uint8_t digest[32]);
char                *pLua_sha256(const char *digest, lua_thread *thread);
static char         value(char c);
int                 pLua_unbase64(unsigned char *dest, const unsigned char *src, size_t srclen);
char                *pLua_decode_base64(const char *src, lua_thread *thread);
char                base64_encode_value(char value_in);
int                 base64_encode_block(const char *plaintext_in, size_t length_in, char *code_out, base64_encodestate *state_in);
int                 base64_encode_blockend(char *code_out, base64_encodestate *state_in);
char                *pLua_encode_base64(const char *src, size_t len, lua_thread *thread);
static int          lua_sha256(lua_State *L);
static int          lua_b64dec(lua_State *L);
static int          lua_b64enc(lua_State *L);
static int          lua_fileexists(lua_State *L);
static int          lua_unlink(lua_State *L);
static int          lua_rename(lua_State *L);
static int          lua_dbclose(lua_State *L);
static int          lua_dbgc(lua_State *L);
static int          lua_dbhandle(lua_State *L);
static int          lua_dbdo(lua_State *L);
static int          lua_dbescape(lua_State *L);
static int          lua_dbquery(lua_State *L);
static int          lua_dbopen(lua_State *L);
static int          lua_header(lua_State *L);
static int          lua_flush(lua_State *L);
static int          lua_sleep(lua_State *L);
static int          lua_echo(lua_State *L);
static int          lua_exit(lua_State *L);
static int          lua_setContentType(lua_State *L);
static int          lua_setErrorLevel(lua_State *L);
static int          lua_setReturnCode(lua_State *L);
static int          lua_httpError(lua_State *L);
static int          lua_getEnv(lua_State *L);
static int          lua_fileinfo(lua_State *L);
static pLuaClock    pLua_getClock(char useAPR);
static int          lua_clock(lua_State *L);
static int          lua_compileTime(lua_State *L);
static int          util_read(request_rec *r, const char **rbuf, apr_off_t *size);
static int          parse_urlencoded(lua_thread *thread, const char *data);
static int          parse_multipart(lua_thread *thread, const char *data, const char *multipart, apr_off_t size);
static int          lua_parse_post(lua_State *L);
static int          lua_parse_get(lua_State *L);
static int          lua_includeFile(lua_State *L);
static void         register_lua_functions(lua_State *L);
void                pLua_create_state(lua_thread *thread);
void                pLua_init_states(lua_domain *domain);
lua_thread          *lua_acquire_state(request_rec *r, const char *hostname);
void                lua_release_state(lua_thread *thread);
static int          plua_handler(request_rec *r);
static void         module_init(apr_pool_t *pool, server_rec *s);
static void         register_hooks(apr_pool_t *pool);
const char          *pLua_set_LuaStates(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_LuaRuns(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_LuaFiles(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_Timeout(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_Logging(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_Multi(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_LogLevel(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_Raw(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_MemoryLimit(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_ShortHand(cmd_parms *cmd, void *cfg, const char *arg);
const char          *pLua_set_Ignore(cmd_parms *cmd, void *cfg, const char *arg);
AP_DECLARE (ap_dbd_t *)
ap_dbd_acquire(request_rec *);
static const command_rec        my_directives[] =
{
    AP_INIT_TAKE1("pLuaStates", pLua_set_LuaStates, NULL, OR_ALL, "Sets the number of Lua states to keep open at all times."),
    AP_INIT_TAKE1("pLuaRuns", pLua_set_LuaRuns, NULL, OR_ALL, "Sets the number of sessions each state can operate before restarting."),
    AP_INIT_TAKE1("pLuaFiles", pLua_set_LuaFiles, NULL, OR_ALL, "Sets the number of lua scripts to keep cached."),
    AP_INIT_TAKE1("pLuaRaw", pLua_set_Raw, NULL, OR_ALL, "Sets a specific file extension to be run as a plain Lua file."),
    AP_INIT_TAKE1
        (
            "pLuaMemoryLimit", pLua_set_MemoryLimit, NULL, OR_ALL,
                "Sets a specific memory limit (in kilobytes) for each thread. Default is 0 (no limit)."
        ),
    AP_INIT_TAKE1
        (
            "pLuaIgnoreLibrary", pLua_set_Ignore, NULL, OR_ALL,
                "Ignores one or more specified Lua core libraries from being loaded into state."
        ),
    AP_INIT_TAKE1("pLuaShortHand", pLua_set_ShortHand, NULL, OR_ALL, "Set to 0 to disable shorthand opening tags. Default is 1 (enabled)"),
    AP_INIT_TAKE1
        (
            "pLuaTimeout", pLua_set_Timeout, NULL, OR_ALL,
                "Sets the maximum number of seconds a pLua script may take to execute. Set to 0 to disable."
        ),
    AP_INIT_TAKE1("pLuaError", pLua_set_Logging, NULL, OR_ALL, "Sets the error logging level. Set to 0 to disable errors, 1 to enable."),
    AP_INIT_TAKE1("pLuaMultiDomain", pLua_set_Multi, NULL, OR_ALL, "Enables or disabled support for domain state pools."),
    AP_INIT_TAKE1
        (
            "pLuaLogLevel", pLua_set_LogLevel, NULL, OR_ALL,
                "Sets the logging level for pLua (0 = disable all, 1 = errors, 2 = module notices, 3 = everything including script errors)."
        ),
    { NULL }
};
module AP_MODULE_DECLARE_DATA   plua_module = { STANDARD20_MODULE_STUFF, NULL, NULL, NULL, NULL, my_directives, register_hooks };
static const luaL_reg           db_methods[] =
{
    { "escape", lua_dbescape },
    { "close", lua_dbclose },
    { "query", lua_dbquery },
    { "run", lua_dbdo },
    { "active", lua_dbhandle },
    { 0, 0 }
};
static const luaL_reg           File_methods[] =
{
    { "rename", lua_rename },
    { "unlink", lua_unlink },
    { "stat", lua_fileinfo },
    { "exists", lua_fileexists },
    { 0, 0 }
};
static const luaL_reg           Global_methods[] =
{
    { "exit", lua_exit },
    { "echo", lua_echo },
    { "print", lua_echo },
    { "flush", lua_flush },
    { "sleep", lua_sleep },
    { "header", lua_header },
    { "setContentType", lua_setContentType },
    { "getEnv", lua_getEnv },
    { "parsePost", lua_parse_post },
    { "parseGet", lua_parse_get },
    { "clock", lua_clock },
    { "compileTime", lua_compileTime },
    { "setReturnCode", lua_setReturnCode },
    { "httpError", lua_httpError },
    { "include", lua_includeFile },
    { "dbopen", lua_dbopen },
    { "showErrors", lua_setErrorLevel },
    { 0, 0 }
};
static const luaL_reg           String_methods[] = { 
    { "SHA256", lua_sha256 }, 
    { "decode64", lua_b64dec }, 
    { "encode64", lua_b64enc }, 
    { 0, 0 } 
};
static const luaL_reg           plualibs[] =
{
    { "", luaopen_base },
    { LUA_LOADLIBNAME, luaopen_package },
    { LUA_TABLIBNAME, luaopen_table },
    { LUA_IOLIBNAME, luaopen_io },
    { LUA_OSLIBNAME, luaopen_os },
    { LUA_STRLIBNAME, luaopen_string },
    { LUA_MATHLIBNAME, luaopen_math },
    { LUA_DBLIBNAME, luaopen_debug },
    { NULL, NULL }
};
#endif /* mod_plua.h */
