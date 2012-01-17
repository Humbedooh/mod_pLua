#!/bin/sh
echo "Compiling..."
gcc    -c -g -fPIC  -MMD -MP -MF main.o.d -o main.o main.c  -I/usr/include/apr-1.0 
gcc     -shared -o mod_plua.so -fPIC main.o -lluajit-5.1 -lapr-1
rm *.o*
echo "All done!"
