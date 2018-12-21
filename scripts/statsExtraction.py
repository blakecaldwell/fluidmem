#!/usr/bin/env python

# Copyright 2016 William Mortl, University of Colorado,  All Rights Reserved
# Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited
# Proprietary and confidential
# Written by William Mortl <william.mortl@colorado.edu>

# imports
from subprocess import check_output
from subprocess import PIPE
from subprocess import Popen
import re
import sys
import time

# configuration constants
DOCKER_CONTAINER_NAME = "monitor_ramcloud"
WAIT_TIME_SECONDS = 10

# constants
CMD_STAT_1 = "docker"
CMD_STAT_2 = "exec"
CMD_STAT_4 = "/scaleos/build/bin/ui"
CMD_STAT_5 = "127.0.0.1"
CMD_STAT_6 = "s"
OUT_ERROR = "ERROR!"
OUT_SUCCESS = "SUCCESS!"

# gets the stats
def cmdGetStats():
	statsText = check_output([CMD_STAT_1, CMD_STAT_2, DOCKER_CONTAINER_NAME, CMD_STAT_4, CMD_STAT_5, CMD_STAT_6])
	return statsText

# handle the data
def handleData(statsList, outFilename):
	# TODO: send stats to remote logging server
	with open(outFilename, "a") as outFile:
		outFile.write("%d," % round(time.time()) + ",".join(statsList) + "\r\n")

# split text into array
def statsTextToDict(statsText):
	ret = []
	splitLines = statsText.split("\n")
	numLines = len(splitLines)
	for i in range(1, numLines):
		if (splitLines[i].strip() != ""):
			valSplit = splitLines[i].split(":")
			ret.append(valSplit[1].strip())
	return ret

# main entry point
if __name__ == "__main__":
	if (len(sys.argv) < 2):
		print("\r\nFluidMem Stat Retrieval by William M Mortl")
		print("Usage: python statsExtraction.py {CSV file name to append to}")
		print("Example: python statsExtraction.py out.csv\r\n")
	else:
		outFile = sys.argv[1]
		while(True):
			print("Getting stats...")
			handleData(statsTextToDict(cmdGetStats()), outFile)
			time.sleep(WAIT_TIME_SECONDS)
