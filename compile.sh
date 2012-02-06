#!/bin/bash
echo "Compiling..."
gcc    -c -g -fPIC  -MMD -MP -MF mod_plua.o.d -o mod_plua.o mod_plua.c  -I/usr/include/apr-1.0 
echo "Select a library to link against:"
select version in "LuaJIT" "Lua 5.1" "Lua 5.2" 
do
if [ "$version" = "LuaJIT" ]; then
	gcc     -shared -o mod_plua.so -fPIC mod_plua.o -lluajit-5.1 -lapr-1
fi
if [ "$version" = "Lua 5.1" ]; then
	gcc     -shared -o mod_plua.so -fPIC mod_plua.o -llua5.1 -lapr-1
fi
if [ "$version" = "Lua 5.2" ]; then
	gcc     -shared -o mod_plua.so -fPIC mod_plua.o -llua5.2 -lapr-1
fi
break
done
 
rm *.o*
echo "All done. If everything went okay, you should have a file called mod_plua.so ready for use."
