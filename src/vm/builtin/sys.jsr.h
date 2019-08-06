// WARNING: this is a file generated automatically by the build process from
// "/home/fabrizio/Workspace/c/blang/src/vm/builtin/sys.jsr". Do not modify.
const char *sys_jsr =
"import io\n"
"var args = []\n"
"native clock()\n"
"native exec(cmd)\n"
"native exit(n=0)\n"
"native gc()\n"
"native getImportPaths()\n"
"native gets()\n"
"native platform()\n"
"native time()\n"
"begin\n"
"    native init()\n"
"    init()\n"
"end\n"
;