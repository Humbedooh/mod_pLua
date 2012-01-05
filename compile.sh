#!/bin/sh
echo "Compiling..."
gcc    -c -g -fPIC  -MMD -MP -MF main.o.d -o main.o main.c
gcc     -shared -o mod_plua.so -fPIC main.o -llua5.1 -lapr-1 -laprutil-1 
rm *.o*
echo "All done!"