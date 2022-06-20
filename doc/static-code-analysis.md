Static code analysis
====================

Zen integrates some tools and build flags that can be used to perform analysis of the source code.

JSON compilation database
-----------------

Some of the tools that are used to perform static code analysis require a JSON file containing the compilation commands and parameters for any source file of the project. In order to generate such file, we have to call `make` through [Bear](https://github.com/rizsotto/Bear).

The easiest way to do it, is by editing the build script `zcutil/build.sh` and editing the last line as:

```
bear -- "$MAKE" "$@" V=1
```

Please note that the project folder should be cleaned before running the build script (using `make clean` or `make distclean`) otherwise the database would not be filled correctly as already compiled files are skipped.

Compiler static analysis
-----------------

The first level of static code analysis is directly performed by the compiler during the build process. The default settings are quite "light", but the analysis level could be increased by adding `--enable-wall` to the `CONFIGURE_FLAGS` environment variable.

It's also possible to make the compilation fail if any warning is raised by using the flag `--enable-werror`.

Clang
-----------------
By default Zen is compiled with GCC, but it's easy to switch to Clang by calling `zcutil/build.sh` with the flag `--use-clang`. Using Clang together with `wall` or `werror` tipically generates a different set of warnings compared with GCC.

Please note that `--use-clang` is only available for Linux, MacOS uses Clang by default through a different build script.

scan-build
-----------------
In addition to the standard compilation warnings, Clang brings some useful static analysis tools like `scan-build`. This tool must be invoked as a wrapper of the `make` command (basically in the same way as **Bear**) by changing the last line of the `zcutil/build.sh` script:

```
scan-build "$MAKE" "$@" V=1
```

At the end of the build process, an HTML report is generated in a temporary folder and it can be inspected with `scan-view`.

clang-tidy
-----------------
Another analysis tool of the Clang family is `clang-tidy`. It can be used to check single files, but the easiest way to run it is through the script `run-clang-tidy.py` which runs the analysis with parallel jobs on the whole project folder.

Note that it requires the `compile_commands.json` database.

Linters
-----------------
The folder `qa/linters` contains some scripts to run additional checks over the source code, the currently available ones are:

- cppcheck
- oclint (requires `compile_commands.json`)
