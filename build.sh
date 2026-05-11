#!/bin/sh

set -e

make -j test
make -j test-win32
