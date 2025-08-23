#include <extlib.h>
#include <stdio.h>

static const char* warning =
    "// WARNING: this is a file generated automatically by the build process, do not modify";

int main(int argc, char** argv) {
    if(argc != 3) {
        fprintf(stderr, "USAGE: %s in out\n", argv[0]);
        return 1;
    }

    const char* in = argv[1];
    const char* out = argv[2];

    StringSlice name = SS(in);
    name = ss_rsplit_once(&name, '/');
    name = ss_split_once(&name, '.');

    StringBuffer in_content = {0};
    if(!read_file(in, &in_content)) return 1;

    StringBuffer out_content = {0};
    sb_appendf(&out_content, "%s\n", warning);
    sb_appendf(&out_content, "const char* " SS_Fmt "_jsc = \"", SS_Arg(name));
    for(size_t i = 0; i < in_content.size; i++) {
        sb_appendf(&out_content, "\\x%02x", (unsigned char)in_content.items[i]);
    }
    sb_append_cstr(&out_content, "\";\n");
    sb_appendf(&out_content, "const size_t " SS_Fmt "_jsc_len = %zu;", SS_Arg(name),
               in_content.size);

    if(!write_file(out, out_content.items, out_content.size)) return 1;

    return 0;
}
