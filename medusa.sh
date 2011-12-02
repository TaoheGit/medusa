#!/bin/sh

PROJECTS_DIR=`pwd`/..
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PROJECTS_DIR}/libchunfeng
#echo ${LD_LIBRARY_PATH}

./medusa -f ${PROJECTS_DIR}/medusa/examples/medusa.conf

