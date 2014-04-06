#New functions in mod_pLua

##Table of Contents

- [clock](#clock) ( )
- [dbopen](#dbopendbtype-parameters) ( dbtype, parameters )
- [echo](#echo) ( ... )
- [exit](#exit) ( )
- [file.exists](#fileexistsfilename) ( filename )
- [file.rename](#filerenamefilename-newfilename) ( filename, newfilename )
- [file.unlink](#fileunlinkfilename) ( filename )
- [file.send](#filesendfilename) ( filename )
- [file.stat](#filestatfilename) ( filename )
- [getEnv](#getenv) ( )
- [getRequestBody](#getrequestbodyfilename) ( filename )
- [header](#headerkey-value) ( key, value )
- [httpError](#httperrorerrorcode) ( errorCode )
- [include](#filename) ( filename )
- [parseGet](#parseget) ( )
- [parsePost](#parsepost) ( )
- [setContentType](#setcontenttypevalue) ( value )
- [setReturnCode](#setreturncoden) ( N )
- [string.encode64](#stringencode64text) ( text )
- [string.decode64](#stringdecode64text) ( text )
- [string.explode](#stringexplodetext-delimiter) ( text, delimiter )
- [string.md5](#stringmd5text) ( text )
- [string.SHA256](#stringsha256text) ( text )

## clock():

Returns a table with the seconds and nanoseconds at the time it was called.

#### Example: 

```lua
    local before = clock()
    do_stuff()
    local after = clock()
    local difference = ((after .seconds*1000000000)+after .nanoseconds) - 
                       ((before .seconds*1000000000)+before .nanoseconds)
    echo("do_stuff() took ", difference/1000000, " milliseconds")
```

##### Precision:
* v<0.12: 1 millisecond on Windows, 1 microsecond on UNIX
* v>=0.13: 1/10th microsecond on Windows, 1 microsecond on UNIX

*Hint: you can use the function compileTime() as you would clock() to get the precise time the mod_plua handler started handling the lua file*

* * *

## dbopen(`dbtype`, `parameters`):

(_See more information on the [Database connectivity](https://github.com/Humbedooh/mod_pLua/blob/master/docs/database.md) page._)  
Tries to open up a connection to a database of type `dbtype` with the parameters set in `parameters`. This function uses APR_DBD to do the actual handling, so you should consult this for a list of supported databases and parameters. The following databases (or handlers) have been successfully tested with mod_pLua:

* sqlite3
* mysql
* oracle
* odbc
* postgresql
* [mod_dbd](http://httpd.apache.org/docs/current/mod/mod_dbd.html)

If an error occurs, open() returns nil as the first argument and an error string explaining the situation as the second argument (though, no error will be returned if "mod_dbd" is the database driver).  
If successful, it returns a table with functions for handling the database:

##### db:active():
Returns true if an active database handle exists, false otherwise.  
Use this to check if your database connection still exists or if it was closed or GC'ed.

##### db:close():
Closes the database connection and cleans up.

##### db:escape(`value`):
Escapes the text specified in `value` for use in SQL statements. The escape routine used is the one native to each database type.

##### db:query(`statement`):
Queries the database using `statement` and returns a table containing all the rows returned.
Each row contains a table of the individual columns fetched as index 1,2,3...  
If query() fails to execute, it will return nil as the first argument and the error message as the second.

##### db:run(`statement`):
Runs `statement` in the opened database and returns the number of rows affected.  
If run() fails to execute, it will return nil as the first argument and the error message as the second.



#### Example: 

```lua
local db = dbopen("sqlite3", "mydatabase.sqlite")
if (db) then
    db:run("INSERT INTO `myTable` VALUES (1,2,3,4,5)")
    local results = db:query("SELECT * FROM `myTable` WHERE 1")
    echo("We got " .. #results .. " results!")
    db:close()
end
```
* * *

#### Using garbage collector to close handles
You can use the garbage collector to close unwanted handled as well, although the preferred way is to use db:close. The example below demonstrates how to create a persistent database connection that reopens if it has been closed:

```lua
db = db or false
if not db or not db:active() then
	 db, err = dbopen("sqlite3", "mydatabase.sqlite")
end
```


## echo(`...`):

Same as `print(...)` Prints out variables to the HTTP stream.

#### Example: 

```lua
echo("Cookies ", "are ", "Awesome", "!", 1+2+3+4+5)
```

* * *

## exit():

Exits the current script, halting any further execution.  
This can, for example, be used to stop a script if conditions are not met

#### Example: 

```lua
    if (user ~= "JohnDoe") then exit() end
    print("This should not show unless you are JohnDoe")
```

* * *

## file.exists(`filename`):

Returns true if the file (or folder) exists, false otherwise.

#### Example: 

```lua
if (file.exists("/path/to/file")) then
    echo("The file exists!")
end
```
* * *

## file.rename(`filename`, `newfilename`):

Renames (moves) the file `filename` to `newfilename`. Returns `true` if successful, `false` otherwise.

#### Example: 

```lua
if (file.exists("/path/to/file")) then
    file.rename("/path/to/file", "/new/path/to/new/file")
end
```
* * *


## file.unlink(`filename`):

Deletes the file `filename`, returns `true` if successful, `false` otherwise.

#### Example: 

```lua
if (file.exists("/path/to/file")) then
    file.unlink("/path/to/file")
end
```
* * *

## file.send(`filename`):

Sends the file `filename` to the client, using [sendfile](http://www.kernel.org/doc/man-pages/online/pages/man2/sendfile.2.html) if possible. On success, returns the number of bytes that was sent, otherwise returns `false`.

Using file.send speeds up the transaction significantly by not having to read the file into a buffer before sending it to the client browser.

#### Example: 

```lua
if (file.exists("/path/to/file")) then
    local bytesSent = file.send("/path/to/file") -- #send file directly to browser
end
```
* * *

## file.stat(`filename`):

Returns a table containing the statistics of `filename`:

* __size:__ The size of the file
* __created:__ The time of creation
* __modified:__ The time of the last modification
* __accessed:__ The time of last access
* __mode:__ The file mode


#### Example: 

```lua
local stat = file.stat("/path/to/file")
if (stat) then
    echo("File was modified at ", stat.modified)
end
```
* * *


## getEnv():

Returns a table of all the environment variables available.

#### Example: 

```lua
local env = getEnv()
echo("You are asking for ", env["Host"])
```

GetEnv also returns information about the mod_plua system, such as its configuration and version.

* * *


## getRequestBody(`[filename]`):

Reads the request body. If `filename` is specified, the request body is written to `filename` instead (useful where memory consumption is a concern), and the total size of the request body is returned.  
If no `filename` is specified, it returns the entire request body as well as the size of the body.   
`getRequestBody()` works only with PUT and POST requests.

#### Example: 

```lua
local bytesWritten = getRequestBody("output.txt")
if (bytesWritten) then 
    print("You sent a file that was ", bytesWritten, " bytes!")
end
```

* * *

## header(`key`, `value`):

Sets the output HTTP header `key` to `value`.

#### Example: 

```lua
header("Set-Cookie", "myCookie=awesome") -- Set a cookie
header("Location", "/somewhere_else") -- redirect to /somewhere_else
```

* * *


## httpError(`errorCode`):

Tells Apache to generate and display an error message that corresponds to the HTTP status code `errorCode`

#### Example: 

```lua
httpError(404) -- Will display a "File not found" message to the browser.
```
If you only wish to set a return code, but not display automatically generated content (for example for basic authorization), you should use `setReturnCode()` instead.


## include(`filename`):

Includes another mod_plua script in the code. This is similar to the PHP include() function, as it preprocesses the included file as it would a regular plua file.
**Note:** This is NOT the same as require() or dofile().

#### Example: 

```lua
echo("Before header include")
include("header.plua")
echo("After header include")
```
* * *

 

## parseGet():

Returns a table containing the key/value pairs from sent via GET
__Note:__ If a key has multiple values, it will be returned as a table, so make sure to check for it, specifically when using \<select\> form fields.

#### Example: 

```lua
local params = parseGet()
if (params) then
    echo("Hello, " .. (params["name"] or "unknown"))
end
```
* * *


## parsePost():

Returns a table containing the key/value pairs from sent via POST.  

#### Example: 

```lua
local params = parsePost()
if (params) then
    echo("Hello, " .. (params["name"] or "unknown"))
end
```
* * *

### Multiple values and files in parseGet() and parsePost()
Any form data with multiple values defined will cause parseGet and parsePost to return this key/value pair as a table with value[1] as the first value, value[2] as the second and so on.  
In case of _multipart/form-data_ posts with files attached, each uploaded file will be returned as a table with value[1] being the filename and value[2] being the contents of the file. In cases where no filename is specified, value[1] will contain the file contents.
  

## setContentType(`value`):

Sets the content type of the output to `value`.

#### Example: 

```lua
setContentType("text/plain")
echo("This is plain text, no &lt;html&gt; will be parsed")
```

* * *

## setReturnCode(`N`):

Sets the HTTP return code to `N`.

#### Example: 

```lua
setReturnCode(401) -- Server now will respond "Authorization required"
```
__note:__ If you wish to invoke server generated replies, such as a 404 message, you should use _httpError()_ instead.

* * *

## string.encode64(`text`):

Encodes `text` using Base64 encoding

#### Example: 

```lua
local txt = string.encode64("Testing")
print(txt) -- Prints 'VGVzdGluZw=='
```
* * *


## string.decode64(`text`):

Decodes `text` using Base64 encoding

#### Example: 

```lua
local txt = string.decode64("VGVzdGluZw==")
print(txt) -- Prints 'Testing'
```
* * *


## string.explode(`text`, `delimiter`):

Chops up a string into bits as marked by `delimiter`. Returns a table with each segment as an element. The value of `delimiter` can be that of a string or a character. If an empty string is given as `delimiter`, the value of `text` will be split into an array with each letter of `text` as an element.

#### Example: 

```lua
local text = "Hello, world!"
local array = string.explode(text, ", ") -- Chop up into "Hello" and "world!"
print(array[2]) -- Print "world!"
```

* * *

## string.md5(`text`):

Creates an MD5 digest based on `text`

#### Example: 

```lua
local txt = string.md5("The quick brown fox jumps over the lazy dog")
print(txt) -- Prints '9e107d9d372bb6826bd81d3542a419d6'
```
* * *

## string.SHA256(`text`):

Creates a SHA256 digest based on `text`

#### Example: 

```lua
local txt = string.SHA256("The quick brown fox jumps over the lazy dog")
print(txt) -- Prints 'd7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592'
```
* * *



