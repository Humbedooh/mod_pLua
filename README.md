mod_pLua
========

mod_pLua is an Apache HTTP Server 2.x module for developing web applications with Lua.
With mod_pLua, you can use Lua for scripting in two distinct ways:

Embedded Lua scripting:
-----------------------

    <html>
        <body>
            <?
              get = parseGet(); --# Parse any GET data passed on to us.
              name = get['name'] or "unknown person";
              x = (x or 0) + 1; --# Increment a persistent, global variable x.
            ?>
            <h1>Hello, <?=name?>!</h1>
            <?if (x > 3) then?>
                X is > 3!
            <?end?>
        </body>
    </html>


Plain Lua scripting:
--------------------

    setContentType("text/html");
    local vars = parseGet();
    local user = vars["name"] or "unknown person";
    print("Hello, ", user);

(This example can be achieved using the pLuaRaw directive)


Additional features
-------------------

mod_pLua precompiles all scripts and caches the compiled binary code so that each new call to the same file will be lightning fast, allowing you to serve hundreds of thousands of requests per minute on any modern server.

Mod_pLua supports both the traditional Lua interpreter as well as LuaJIT for both Windows and UNIX platforms.
If your web server supports it, mod_pLua also utilizes APR_DBD and mod_dbd to handle databases through the dbopen() Lua function. And last, but not least, mod_pLua is of course thread-safe.