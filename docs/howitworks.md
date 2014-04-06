#How mod_pLua works

##Preprocessing
mod_pLua is first and foremost a preprocessor. It processes a hypertext markup file with embedded Lua into pure Lua code by chopping up the file into small segments; HTML/XML code and Lua code:
http://www.humbedooh.com/lua/preprocessing_plua.png
_(This is a very simplified example of how the preprocessing works. In reality, it's a lot more complex)_

Line numbers are preserved, so any Lua error will respond to the exact same line as in the original script file.

##Compiling and caching
Once preprocessed, the file is compiled using the Lua interpreter available on the system. If no compiler errors were found, the script is stored as compiled bytecode in the mod_pLua cache for the given domain pool, for any later use.
http://www.humbedooh.com/lua/plua_handling.png

##Running the code
After the processed script has been compiled, standard libraries and functions are loaded into a Lua state (if not present already), and the script is called from the cache.

##mod_pLua or mod_lua?
mod_pLua and mod_lua are two separate modules for Apache, and although they share the same love for Lua, they have different approaches to using Lua for web development.

__`mod_pLua` is for:__

* Script or hypertext oriented development as with PHP, ASP, JSP etc.
* Flow based execution of scripts (script is run as read).
* Automatic handling of request and file cache.
* Developing applications that make use of persistent database connections.

__`mod_lua` is for:__

* Creating your own Lua handlers for Apache as with mod_perl etc.
* Function based execution of scripts (requires a handler or filter function that controls the flow).
* Manual handling of requests and any cache system you may need.

__Notable differences in design:__

* __State handling:__ `mod_lua` uses a _per-thread_ created state for execution while `mod_pLua` uses a _per-host_ pool of states that threads share.
* __Libraries:__ `mod_lua` requires opening of any library needed for operations (including `string`, `table` etc) while `mod_pLua` comes batteries-included (unless specifically told otherwise with the `pLuaIgnoreLibrary` directive).

As written above, you can sum the differences up like so: mod_pLua is the mod_php of Lua, while mod_lua is the mod_perl of Lua (although they are not as bloated).