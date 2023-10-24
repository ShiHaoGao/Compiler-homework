#!/usr/bin/env bash

#cd build || exit
cmake --build build

./build/ast-interpreter "$(cat ./test/test00.c)"
