#!/bin/sh
# Builds and runs the host-side tests (needs any C++17 compiler).
set -e
cd "$(dirname "$0")"
mkdir -p build
${CXX:-g++} -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined -o build/ips_test ips_test.cpp ../app/source/homestead/ips.cpp 2>/dev/null \
  || ${CXX:-g++} -std=c++17 -Wall -Wextra -O1 -o build/ips_test ips_test.cpp ../app/source/homestead/ips.cpp
./build/ips_test
