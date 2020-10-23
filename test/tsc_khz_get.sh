#!/bin/sh
gdb /dev/null /proc/kcore -ex 'x/uw 0x'$(grep '\<tsc_khz\>' /proc/kallsyms | cut -d' ' -f1) -batch 2>/dev/null | tail -n 1 | cut -f2
