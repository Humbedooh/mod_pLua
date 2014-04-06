#Setting up mod_pLua

## Compiling on UNIX platforms
To compile mod_pLua on UNIX platforms, run `make` using the following targets:

* To compile with Lua 5.1, run `make 5.1`
* To compile with Lua 5.2, run `make 5.2`
* To compile with LuaJIT, run `make luajit`

###Compiling with mod_dbd support
mod_pLua supports the use of mod_dbd for managing database connections. To enable this, simple run the following command prior to calling `make`:

    export PLUADBD=1

## Installing mod_pLua
###Linux
run `make install` or copy mod_plua.so to your Apache 'modules' folder.

### Windows
Copy mod_plua.so to your Apache 'modules' folder.
You may need to copy msvcr100.dll, lua51.dll and/or lua52.dll to you apache2 'bin' folder

## Loading mod_pLua into Apache
Assuming you have either downloaded the compiled module or compiled it yourself, you can enable it in Apache2 by adding the following line to your apache configuration:

    LoadModule    plua_module modules/mod_plua.so

## Setting up the parser
Once enabled, you can set your files to be handled by the module:

```
<FilesMatch "\..*lua$">
    SetHandler plua
</FilesMatch>
```

Another way to add the pLua handler to a file type is to use the AddHandler directive:

    AddHandler plua .lua

## Fine-tuning mod_pLua
mod_pLua comes with a set of default values that can be changed in the apache config:

#####pLuaStates &nbsp;`N`
Sets the available amount of Lua states to `N` states. Default is 50. pLuaStates is by default configured for the threaded MPM version. If you are using prefork MPM, you may set this number to 1 to save memory.

In mod_pLua 0.53 or above, setting this value to 0 will create a per-thread Lua state instead of managing a global per-server pool. On certain systems, such as the event MPM on UNIX, this may improve your performance significantly.

#####pLuaRuns &nbsp;`N`
Restarts Lua States after `N` sessions. Default is 1000

#####pLuaFiles &nbsp;`N`
Sets the file cache array to hold `N` elements. Default is 50.  
Each 100 elements take up 30kb of memory, so having 200 elements in 50 states will use 3MB of memory. If you run a large server with many scripts, you may want to set this to a higher number, fx. 250

#####pLuaTimeout &nbsp;`N`
Sets a timeout threshold of `N` seconds. If a script takes longer than this, it will be stopped.
The default value for this directive is 0 (disable), as it will incur a 10% performance loss when activated, due to the debug hooks. Set it to something sensible if you must, 5-10 seconds should suffice.

#####pLuaRaw &nbsp;`ext`
Sets the handler to compile any file with the extension `ext` as plain Lua files. Remember to add the leading dot to the directive, as such:

    pLuaRaw .lua

#####pLuaError &nbsp;`N`
Sets the error reporting level to `N`. Set to 0 to disable all error output and 1 (default) to enable compiler/run-time errors to be sent to the client.

#####pLuaLogLevel &nbsp;`N`
Sets the internal log reporting level to `N`:

* 0 = disable all
* 1 = errors
* 2 = module notices
* 3 = everything including script errors

#####pLuaMultiDomain &nbsp;`N`
Enables or disables support for multiple domain pools (one for each vhost) for retaining separate states for each domain. Set to 1 to enable, 0 to disable. The default value is 1 (enabled). 

#####pLuaShortHand &nbsp;`N`
By default, mod_pLua supports both long and short opening tags:  
`<?doStuff()?>` is equal to `<?lua doStuff()?>`  
`<?=variable?>` is equal to `<?lua=variable?>`  

pLuaShortHand enables or disables support for short hand opening tags: `<?` and `<?=`.  
Set to 1 to enable, 0 to disable. The default value is 1 (enabled). 

#####pLuaMemoryLimit &nbsp;`N`
Sets the memory limit of each Lua state to `N` kilobytes. As with pLuaTimeout, this directives requires monitoring of each Lua state during execution, so you should be mindful of a minor performance impact.

#####pLuaIgnoreLibrary &nbsp;`lib`
Disables loading the library or libraries specified by `lib` into the Lua states. This is useful for sandboxing environments where you don't want users to, for example, override the memory limit or the script timeout value. Libraries are separated by commas or spaces, for example:

    pLuaIgnoreLibrary "os debug" # Ignore both os.* and debug.*
