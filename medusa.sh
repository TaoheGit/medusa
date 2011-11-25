#!/bin/sh

PROJECTS_DIR=`pwd`/../../..
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PROJECTS_DIR}/libchunfeng/src/libchunfeng
#echo ${LD_LIBRARY_PATH}

./medusa -f ${PROJECTS_DIR}/medusa/src/medusa/examples/medusa.conf

