import sys


def error(msg):
    print(msg)
    sys.exit(1)


def toCString(line):
    line = line.strip()
    if line and not line.startswith('//'):
        # escape
        line = line.replace("\\", "\\\\").replace('"', '\\"')
        return '"{}\\n"'.format(line)
    else:
        return ''


if len(sys.argv) < 2:
    error("No input file")
elif len(sys.argv) < 3:
    error("No output file")

fileName = sys.argv[1].split('/').pop().replace('.', '_')

WARNING = "// WARNING: this is a file generated automatically by the build process. Do not modify."
includeBuilder = [WARNING, 'const char *{} ='.format(fileName)]

with open(sys.argv[1], 'r') as src:
    for line in src:
        includeLine = toCString(line)
        if includeLine:
            includeBuilder.append(includeLine)
    includeBuilder.append(';')

with open(sys.argv[2], 'w') as out:
    out.write('\n'.join(includeBuilder))
