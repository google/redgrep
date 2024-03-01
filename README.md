# redgrep

## About

redgrep is a grep based on regular expression derivatives. That is, it uses
regular expression derivatives to construct the DFA. It then uses LLVM to JIT
the DFA.

Since regular expression derivatives permit the three basic Boolean operations
of disjunction (`|`), conjunction (`&`) and complement (`!`), redgrep enables
you to write very powerful regular expressions very easily and guarantees to
match them in linear time.

## Building

You must have Bazel, GNU bison and either GCC or Clang.

redgrep attempts to keep up with LLVM development, so you should
[get the source code and build LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm).
(Debian and Ubuntu users might prefer to install the
[nightly packages](https://apt.llvm.org/) instead.)

`llvm-config-17` must be in your path.

## Contact

[redgrep@googlegroups.com](mailto:redgrep@googlegroups.com)

## Disclaimer

This is not an official Google product.
