import os.path
import re
from argparse import ArgumentParser

WARNING = "// WARNING: this is a file generated automatically by the build process, do not modify"


def minimize_line(line):
    return re.sub("//.*", "", line).strip()


def escape_line(line):
    return line.replace("\\", "\\\\").replace('"', '\\"')


def to_c_string(line):
    line = minimize_line(line)
    line = escape_line(line)
    return '"{0}\\n"'.format(line) if line else ""


argparser = ArgumentParser()
argparser.add_argument("file", help="The text file to convert")
argparser.add_argument("out", help="The name of the generated header")

args = argparser.parse_args()

name = os.path.basename(args.file).replace(".", "_")
include_builder = [WARNING, "const char* {} =".format(name)]

with open(args.file, "r") as f:
    for line in f:
        c_line = to_c_string(line)
        if c_line:
            include_builder.append(c_line)

    include_builder.append(";")

with open(args.out, "w") as out:
    out.write("\n".join(include_builder))
