#!/usr/bin/env bash

git submodule update --init --recursive

cargo build --release

xmake -y

