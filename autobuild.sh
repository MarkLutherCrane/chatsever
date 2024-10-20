#!/bin/bash
rm -rf `pwd`/build/*
cd `pwd`/build &&
	cmake .. &&
	make
