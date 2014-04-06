#Database connectivity
mod_pLua provides methods to connect to the most popular databases through the [APR_DBD](http://people.apache.org/~niq/dbd.html) API and [mod_dbd](http://httpd.apache.org/docs/current/mod/mod_dbd.html).  
The following database handlers are available, if supported by your Apache HTTP Server:  

* MySQL 5.x (`mysql`)
* SQLite3 (`sqlite3`)
* PostgreSQL (`pgsql`)
* Oracle (`oracle`)
* FreeTDS (`freetds`)
* ODBC Connector (`odbc`)
* mod_dbd Connector (`mod_dbd`)   

## Connecting to a database
All connections are initialized through the [`dbopen`](https://sourceforge.net/p/modplua/wiki/New%20functions%20in%20pLua/) function which takes two arguments:

* The database driver/connector to use
* Database specific parameters

The following examples show how to connect to a range of databases:

```lua
local db = dbopen("sqlite3", "/path/to/mydatabase.sqlite") -- SQLite3
local db2 = dbopen("mysql", "host=localhost,user=root,database=myDataBase") -- MySQL
local db3 = dbopen("mod_dbd", "") -- mod_dbd connector
```

If successful, `dbopen` returns a handle to the database with functions to be used. Otherwise, it returns nil and an error message.

## Executing a statement
Statements, that do not return a set of rows, are executed via `db:run(_statement_)`. This function will pass the statement onto the database driver and return the number of rows affected, as such:

```lua
local rowsAffected, err = db:run("DELETE FROM `myTable` WHERE 1")
if not rowsAffected then
    print("Something went wrong: ", err)
else
    print("We deleted ", rowsAffected, " rows!")
end
```

You will notice that, should either `db:run` or `db:query` fail, it will return a nil value followed by an error message. Thus, you should always check that the returned value is a number before proceding with your code.  
   
## Fetching data
Statements such as SELECT, that return a set of rows, are executed via `db:query(_statement_)`. Any rows returned will be so in a table with the first row at index 1 and the first cell at index 1 in the sub-table. To get the number of rows returned, simple use #rows, as shown in this example:

```lua
local db = dbopen("sqlite3", "mydb.sqlite")
if db then
    local rows, err = db:query("SELECT `name`, `phone`, `address` FROM `myTable` WHERE 1")
    if rows then
        print("I found ", rows, " records matching the criteria!\n")
        for i, row in pairs(rows) do
            print("Name: ", row[1], "\n")
            print("Phone: ", row[2], "\n")
            print("Address: ", row[3], "\n")
        end
    else
        print("Something went wrong: ", err)
    end
end
```

## Cleaning up
When you are done using your connection, close it using `db:close()`, as shown here:

```lua
local database = dbopen("mysql", "some connection string") -- Open the connection
database:run("some statement") -- Do your stuff
database:close() -- Done!
```

Should you forget to call `db:close()`, the garbage collector will most likely do this for you at some point in time, but you risk having several open, unused connections lingering about if you choose not to explicitly close them.