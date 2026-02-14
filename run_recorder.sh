#!/bin/bash
python3 src_py/bybit_feed.py | ./bin/recorder "$@"
