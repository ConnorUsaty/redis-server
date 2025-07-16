#!/bin/bash

# builds everything through cmake
mkdir -p build
cd build
cmake ..
make
cd ..
