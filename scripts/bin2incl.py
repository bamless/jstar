#!/usr/bin/env python

import os.path
import re
from argparse import ArgumentParser

WARNING = "// WARNING: this is a file generated automatically by the build process, do not modify\n"

LINE_WIDTH = 15
LINE_INDENT = " " * 4

argparser = ArgumentParser()
argparser.add_argument("file", help="The text file to convert")
argparser.add_argument("out", help="The name of the generated header")

args = argparser.parse_args()

name = os.path.basename(args.file).replace(".", "_")
include_builder = [WARNING]

with open(args.file, "rb") as f:
    # convert binary file to hex byte array
    include_builder.append(f"char {name}[] = {{")

    hex_written = 0
    byte = f.read(1)

    while byte:
        if hex_written % LINE_WIDTH == 0:
            include_builder.append(f"\n{LINE_INDENT}")

        include_builder.append(f"0x{byte.hex()}")
        include_builder.append(", ")

        byte = f.read(1)
        hex_written += 1

    include_builder.append("\n")
    include_builder.append("};")
    include_builder.append("\n")

    # write file length
    include_builder.append(
        f"const unsigned int {name}_len = {os.path.getsize(args.file)};")


with open(args.out, "w") as out:
    out.write("".join(include_builder))
