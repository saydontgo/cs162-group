#! /bin/bash

make
if [ $2 -eq 1 ]
then 
pintos-test $1
else 
PINTOS_DEBUG=1 pintos-test $1
fi
