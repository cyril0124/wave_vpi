#!/usr/bin/env bash

git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh

./vcpkg/vcpkg install fmt
./vcpkg/vcpkg install libassert
./vcpkg/vcpkg install catch2

./vcpkg/vcpkg install boost-stacktrace
./vcpkg/vcpkg install libbacktrace