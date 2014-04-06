mod_pLua
========

mod_pLua is an Apache HTTP Server 2.x module for developing web applications with Lua.
With mod_pLua, you can use Lua for scripting in two distinct ways:

Embedded Lua scripting:
-----------------------

    <html>
        <body>
            <?
              get = parseGet() -- Parse any GET data passed on to us.
              name = get['name'] or "unknown person"
              x = (x or 0) + 1 -- Increment a persistent, global variable x.
            ?>
            <h1>Hello, <?=name?>!</h1>
            <?if (x > 3) then?>
                X is > 3!
            <?end?>
        </body>
    </html>


Plain Lua scripting:
--------------------

```lua
setContentType("text/html")
local vars = parseGet()
local user = vars["name"] or "unknown person"
print("Hello, ", user)
```

(This example can be achieved using the [pLuaRaw](https://github.com/Humbedooh/mod_pLua/blob/master/docs/setup.md#pluaraw-ext) directive)


Additional features
-------------------

mod_pLua [precompiles all scripts and caches the compiled binary code](https://github.com/Humbedooh/mod_pLua/blob/master/docs/howitworks.md) so that each new call to the same file will be lightning fast, allowing you to serve hundreds of thousands of requests per minute on any modern server.

Mod_pLua supports both the traditional Lua interpreter as well as LuaJIT for both Windows and UNIX platforms.
If your web server supports it, mod_pLua also utilizes APR_DBD and mod_dbd to handle databases through the [dbopen()]((https://github.com/Humbedooh/mod_pLua/blob/master/docs/howitworks.md)) Lua function. And last, but not least, mod_pLua is of course thread-safe.

See also
--------

You may want to look at these pages before you start:

* [mod_pLua examples](https://github.com/Humbedooh/mod_pLua/blob/master/docs/examples.md)
* [Setting up mod_pLua](https://github.com/Humbedooh/mod_pLua/blob/master/docs/setup.md)
* [New functions in mod_pLua](https://github.com/Humbedooh/mod_pLua/blob/master/docs/functions.md)
* [Database connectivity](https://github.com/Humbedooh/mod_pLua/blob/master/docs/database.md)
* [How mod_pLua works](https://github.com/Humbedooh/mod_pLua/blob/master/docs/howitworks.md)