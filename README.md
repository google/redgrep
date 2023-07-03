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

If `llvm-config-15` is in your path, add the following to your `WORKSPACE` file:

```
load("@com_github_google_redgrep//:redgrep_configure.bzl", "redgrep_configure")
redgrep_configure(name = "local_config_redgrep")
```

Otherwise, add the following to your `WORKSPACE` file and specify the path to
`llvm-config-15`:

```
load("@com_github_google_redgrep//:redgrep_configure.bzl", "redgrep_configure")
redgrep_configure(name = "local_config_redgrep", llvm_config = "/path/to/llvm-config-15")
```

## Contact

[redgrep@googlegroups.com](mailto:redgrep@googlegroups.com)

## Disclaimer

This is not an official Google product.
