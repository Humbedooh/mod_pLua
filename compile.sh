#!/bin/sh
echo "Compiling..."
gcc    -c -g -fPIC  -MMD -MP -MF mod_plua.o.d -o mod_plua.o mod_plua.c  -I/usr/include/apr-1.0 
if [ "$1" = "-jit" ]; then
	gcc     -shared -o mod_plua.so -fPIC mod_plua.o -lluajit-5.1 -lapr-1
else
	gcc     -shared -o mod_plua.so -fPIC mod_plua.o -llua5.1 -lapr-1
fi
rm *.o*
echo "All done!"
