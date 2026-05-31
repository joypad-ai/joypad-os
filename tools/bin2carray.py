#!/usr/bin/env python3
# Convert a raw .bin into a C array source + header for embedding.
# Usage: bin2carray.py <input.bin> <output.c> <output.h> <symbol>
# Emits:  const unsigned char <symbol>[] = {...};
#         const unsigned int  <symbol>_length = N;
import sys

if len(sys.argv) != 5:
    sys.stderr.write("usage: bin2carray.py <in.bin> <out.c> <out.h> <symbol>\n")
    sys.exit(2)

inp, out_c, out_h, sym = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

with open(inp, "rb") as f:
    data = f.read()

with open(out_c, "w") as f:
    f.write('#include "%s"\n\n' % out_h.split("/")[-1])
    f.write("const unsigned char %s[] = {\n" % sym)
    for i in range(0, len(data), 16):
        row = ", ".join("0x%02x" % b for b in data[i:i + 16])
        f.write("    %s,\n" % row)
    f.write("};\n")
    f.write("const unsigned int %s_length = %d;\n" % (sym, len(data)))

with open(out_h, "w") as f:
    guard = sym.upper() + "_H"
    f.write("#ifndef %s\n#define %s\n" % (guard, guard))
    f.write("extern const unsigned char %s[];\n" % sym)
    f.write("extern const unsigned int %s_length;\n" % sym)
    f.write("#endif\n")
