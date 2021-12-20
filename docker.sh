#!/bin/bash

PWD=$(readlink -f .)
USER="$(id -u):$(id -g)"

docker run -it --name docker-compile --rm \
	--user ${USER} -v ${PWD}:/volume -w /volume \
	gcc:10.3 make all
