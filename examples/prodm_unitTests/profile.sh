#!/bin/sh

APP=./client
PPROF=/home/tolyan/src/gperftools/src/pprof


export CPUPROFILE=client.prof
export CPUPROFILE_REALTIME=1

$APP $@
$PPROF --web $APP $CPUPROFILE
