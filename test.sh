#!/usr/bin/env bash

#cd build || exit
#cmake --build build

#test_file="./test/test24.c"
#
#echo "================================AST================================"
#
#../llvm-10/build/bin/clang -Xclang -ast-dump -fsyntax-only $test_file
#
#echo
#echo "================================AST-END================================"

for test_file in ./test/*
do
  if [ -f "$test_file" ]; then
    echo "================================Output:$test_file================================"

    ./build/ast-interpreter "$(cat $test_file)"

    echo

    echo "================================Output-END:$test_file================================"
  fi
done




