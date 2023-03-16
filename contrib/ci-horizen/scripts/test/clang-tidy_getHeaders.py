#!/usr/bin/env python3

import os
import glob

# Scans zen/src for .h and .hpp filenames.
# Returns a string to be passed to the -header-filter option of clang-tidy
# ex.: .*/standard.h|.*/ecmult_gen.h|.*/hash.h|.*/clientversion.h

h_files = glob.glob("./src/**/*.h", recursive=True)
hpp_files = glob.glob("./src/**/*.hpp", recursive=True)
fileset = set()
for filepath in h_files:
    fileset.add(filepath.split(sep=os.sep)[-1])
for filepath in hpp_files:
    fileset.add(filepath.split(sep=os.sep)[-1])

outstr = "|.*\/".join(fileset)
outstr = ".*\/" + outstr

# Return the list to clang-tidy-launcher.sh
print(outstr)
