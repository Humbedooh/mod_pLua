# mod_pLua #

mod_pLua is an Apache HTTP Server 2.x module for developing web applications with Lua.

It has a [different approach](https://github.com/Humbedooh/mod_pLua/blob/master/docs/howitworks.md#mod_plua-or-mod_lua) than mod_lua and works similar to mod_php.

With mod_pLua, you can use Lua for scripting in two distinct ways:

## Embedded Lua scripting:

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


## Plain Lua scripting:

```lua
setContentType("text/html")
local vars = parseGet()
local user = vars["name"] or "unknown person"
print("Hello, ", user)
```

(This example can be achieved using the [pLuaRaw](https://github.com/Humbedooh/mod_pLua/blob/master/docs/setup.md#pluaraw-ext) directive)

## Additional features

mod_pLua [precompiles all scripts and caches the compiled binary code](https://github.com/Humbedooh/mod_pLua/blob/master/docs/howitworks.md) so that each new call to the same file will be lightning fast, allowing you to serve hundreds of thousands of requests per minute on any modern server.

mod_pLua supports both the traditional Lua interpreter as well as LuaJIT for both Windows and UNIX platforms.
If your web server supports it, mod_pLua also utilizes APR_DBD and mod_dbd to handle databases through the [dbopen()](https://github.com/Humbedooh/mod_pLua/blob/master/docs/functions.md#dbopendbtype-parameters) Lua function. And last, but not least, mod_pLua is of course thread-safe.

## Download mod_pLua

Below you can find the various package solutions available for download.

The currently available options are:

* **[Windows binaries](https://sourceforge.net/projects/modplua/files/Windows%20binaries/):** Compiled binaries for Windows 32/64 bit.
* **[Linux binaries](https://sourceforge.net/projects/modplua/files/Linux%20binaries):** Compiled Linux binaries for i686 and x86_64.
* **[FreeBSD binaries](https://sourceforge.net/projects/modplua/files/FreeBSD%20binaries):** Compiled FreeBSD 9.0 binaries for x86_64.

### Which version to choose
####UNIX users
If you are running Linux or FreeBSD, you can download the compiled binaries - otherwise, download the source and run ./configure followed by make.

####Windows
If you are running Windows, you can download a compiled package for either Lua or LuaJIT (recommended).
You will need to copy the DLL files in your package into the Apache2 'bin' folder.

####Other architectures
If you have access to other architectures than the ones listed, and you have sufficient knowledge to compile the binaries, do let me know, and I'll upload your copies of the binaries!

###Requirements
####Lua versions
The Lua versions require Lua 5.1 or 5.2 installed on your machine, depending on which package you download.

####LuaJIT versions
The LuaJIT versions for Windows come bundled with the LuaJIT library (lua51.dll) for quick installation.  
For UNIX, you will have to install LuaJIT yourself using your local package manager.

####Installing
To install mod_plua, simply copy the compiled .so file to your apache module folder, typically /usr/lib/apache2/modules.
For configuration, please see **[Setting up mod_plua](https://github.com/Humbedooh/mod_pLua/blob/master/docs/setup.md)**.

## See also

You may want to look at these pages before you start:

* [mod_pLua examples](https://github.com/Humbedooh/mod_pLua/blob/master/docs/examples.md)
* [Setting up mod_pLua](https://github.com/Humbedooh/mod_pLua/blob/master/docs/setup.md)
* [New functions in mod_pLua](https://github.com/Humbedooh/mod_pLua/blob/master/docs/functions.md)
* [Database connectivity](https://github.com/Humbedooh/mod_pLua/blob/master/docs/database.md)
* [How mod_pLua works](https://github.com/Humbedooh/mod_pLua/blob/master/docs/howitworks.md)