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

# configuration constants
NOVA_URL = "http://10.0.1.1:35357"
NOVA_USERNAME = "admin"
NOVA_PASSWORD = "8aa2f64f55951e7a4956"
NOVA_TENANT = "admin"
REMOTE_FILENAME_PATH = "/tmp/hotplug.xml"

# constants
CMD_SSH_1 = "ssh"
CMD_SSH_3 = "-t"
CMD_NOVA_SHOW_1 = "nova"
CMD_NOVA_SHOW_2 = "--os-auth-url"
CMD_NOVA_SHOW_4 = "--os-user-name"
CMD_NOVA_SHOW_6 = "--os-password"
CMD_NOVA_SHOW_8 = "--os-tenant-name"
CMD_NOVA_SHOW_10 = "show"
NOVA_ERROR = "ERROR"
NOVA_SUCCESS = "+-"
NOVA_HOST = "OS-EXT-SRV-ATTR:hypervisor_hostname"
NOVA_INSTANCE_NAME = "OS-EXT-SRV-ATTR:instance_name"
CMD_VIRSH = "docker exec nova_scaleos virsh attach-device %s %s"
CMD_CREATEFILE = "docker exec nova_scaleos bash -c \"echo \\\"%s\\\" > %s\""
CREATEFILE_SUCCESS = "Device"
XML_TEMPLATE = "<memory model='dimm'><source><isElastic>%s</isElastic></source><target><size unit='KiB'>%s</size><node>%s</node></target></memory>"
OUT_ERROR = "ERROR!"
OUT_SUCCESS = "SUCCESS!"
ELASTIC_VAR_ERR = "Value for isElastic is not a Zero or One!"


def is_zero_or_one(var):
    try:
        var = int(var)
        if var == 0 or var == 1:
            return True
        else:
            return False
    except Exception:
        return False

# query and get values for the UUID, returns key-value dictionary
def cmdQueryUUID(uuid):
	retDict = {}
	outputText = check_output([CMD_NOVA_SHOW_1, CMD_NOVA_SHOW_2, NOVA_URL, CMD_NOVA_SHOW_4, NOVA_USERNAME, CMD_NOVA_SHOW_6, NOVA_PASSWORD, CMD_NOVA_SHOW_8, NOVA_TENANT, CMD_NOVA_SHOW_10, uuid])
	if (outputText.startswith(NOVA_ERROR) == True):
		retDict = {}
	else:
		retDict = textTableToDictionary(outputText)
	return retDict

# ssh and run a command
def cmdSSHRunCommand(host, command):
	ssh = Popen([CMD_SSH_1, host, CMD_SSH_3, command], shell = False, stdout = PIPE, stderr = PIPE)
	return ssh.stdout.readlines()

# generates a remote file
def cmdSSHCreateRemoteFile(host, fileName, fileText):
	return cmdSSHRunCommand(host, CMD_CREATEFILE % (fileText, fileName))

# virsh attach
def cmdSSHVirshAttach(host, instanceName, fileName):
	return cmdSSHRunCommand(host, CMD_VIRSH % (instanceName, fileName))

# split table into dictionary
def textTableToDictionary(textTable):
	retDict = {}

	# cleanup and split to an array by \r\n
	textTable.strip()
	lines = textTable.split("\n")

	# loop through lines, skip first 3 and last line
	for i in range(3, len(lines) - 1):
		elements = re.sub(r"( )+", r"", lines[i].strip()).split("|")
		if (elements[0].strip() == ""):
			retDict[elements[1].strip()] = elements[2].strip()

	return retDict

# build XML file
def buildXML(isElastic, memInKB, numaNode):
	return XML_TEMPLATE % (isElastic, memInKB, numaNode)

def usage():
	print("\r\nFluidMem Memory Attachment by William M Mortl")
	print("Usage: python attachMemory.py {uuid} {is elastic (1) or normal hotplug (0)} {memory to add, in KB, needs magic number} {NUMA node to attach memory to} {OPTIONAL: debug mode}")
	print("Example: python attachMemory.py 81178047-35d0-49ee-affe-9cfbceaeda41 512 1 True\r\n")
	sys.exit(1)

# main entry point
if __name__ == "__main__":
	if (len(sys.argv) < 3):
		usage()
	else:

		# extract command line parameters
		uuid = sys.argv[1]
		isElastic = sys.argv[2]
		if not is_zero_or_one(isElastic):
			print(ELASTIC_VAR_ERR)
			usage()

		memInKB = sys.argv[3]
		numaNode = sys.argv[4]
		debugMode = (len(sys.argv) >= 6)

		# get key-value pairs of values
		uuidValues = cmdQueryUUID(uuid)
		if (debugMode == True):
			print(uuidValues)
		if (len(uuidValues) <= 0):
			print(OUT_ERROR)
			sys.exit(1)

		# connect and transmist XML
		retVal = cmdSSHCreateRemoteFile(uuidValues[NOVA_HOST], REMOTE_FILENAME_PATH, buildXML(isElastic, memInKB, numaNode))
		if (debugMode == True):
			print(retVal)
		if (retVal != []):
			print(OUT_ERROR)
			sys.exit(1)

		# execute "docker exec nova_scaleos virsh device-attach uuidValues[NOVA_INSTANCE_NAME] REMOTE_FILENAME_PATH"
		retVal = cmdSSHVirshAttach(uuidValues[NOVA_HOST], uuidValues[NOVA_INSTANCE_NAME], REMOTE_FILENAME_PATH)
		if (debugMode == True):
			print(retVal)
		if (retVal[0].startswith(CREATEFILE_SUCCESS) == False):
			print(OUT_ERROR)
			sys.exit(1)

		# success!
		print(OUT_SUCCESS)
