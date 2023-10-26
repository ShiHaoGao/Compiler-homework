#!/usr/bin/env bash

#cd build || exit
#cmake --build build

test_file="./test/test01.c"

echo "================================AST================================"

../llvm-10/build/bin/clang -Xclang -ast-dump -fsyntax-only $test_file

echo
#echo "================================AST-END================================"

echo "================================Output================================"

./build/ast-interpreter "$(cat $test_file)"

echo

echo "================================Output-END================================"