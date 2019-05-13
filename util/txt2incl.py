#!/usr/bin/env python2

import sys

def error(msg):
	print msg
	sys.exit(1)

def stringify(line):
	if line.endswith("\n"):
		line = line[:len(line) - 1]
	if not line: 
		return line
	line = line.replace("\\", "\\\\")
	line = line.replace('"', '\\"')
	return '"' + line + '\\n"'

if len(sys.argv) < 2:
	error("No input file")
elif len(sys.argv) < 3:
	error("No output file")

WARNING = ("// WARNING: this is a file generated automatically by the build "
"process from\n// \"{}\". Do not modify.\n").format(sys.argv[1])

fileName = sys.argv[1].split('/')
fileName = fileName[len(fileName) - 1]
fileName = fileName.replace('.', '_')

header = WARNING + "const char *{} =\n".format(fileName)

with open(sys.argv[1], "r") as src:
	for line in src:
		cstr = stringify(line)
		header += cstr + '\n' if cstr else ""
	header += ';'

with open(sys.argv[2], "w") as out:
	out.write(header)
