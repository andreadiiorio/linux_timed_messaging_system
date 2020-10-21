#!/bin/bash
make clean && make install
bash createDevFiles.sh 
make test 
