#mod_pLua examples

##Printing environment variables

```
Let's print out <i>all</i> the environment vars we get from the HTTP request:<br/>
<?
local env = getEnv()
for key,value in pairs(env) do
    ?>
        <b><?=key?>:</b> <?=value?><br />
    <?
end
?>
```

##Reading form data

```lua
<form method="post">
<input type="text" name="name"/>
<input type="hidden" name="action" value="post stuff">
<input type="submit" value="Submit"/>
</form>
<?
local params = parsePost()
if (params and params["name"]) then
    echo("Hello, ", params["name"], "!")
end
?>
```

##Printing a text-only page

```lua
<?
setContentType("text/plain")
?>
Plain text goes here, and any <html> tags should not be parsed.
```

##Sending a file directly to the browser

```lua
local filename = "abcdefg.txt"
setContentType("text/plain")
file.send(filename) -- Use sendfile to maximize performance
```

##Including scripts and files

```lua
<?
include("myheader.plua")
echo("Contents goes here")
include("footer.html") -- You can include HTML files as well
?>
```

##Redirecting to another URL

```lua
<?
header("Location", "/gone")
setReturnCode(302) --Set httpd to return 302 Moved
print("We have moved!")
?>
```

##Basic WWW-authorization

```lua
<?
header("WWW-Authenticate", 'Basic realm="Super secret zone"')
local env = getEnv() or {}
local auth = string.decode64((env['Authorization'] or ""):sub(7))
local _,_,user,pass = auth:find("([^:]+)%:([^:]+)")  
if (not user or not pass) then
    setReturnCode(401)
    print("Please enter a username and a password!")
    exit()
end
print("Hello, ", user, "who entered the password: ", pass )
?>
```