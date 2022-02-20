#!/bin/bash
set -e
#build
rm build -r
cmake -S . -B build
cmake --build build
#install
sudo $(which cmake) --install build