# redgrep

## About

redgrep is a grep based on regular expression derivatives. That is,
it uses regular expression derivatives to construct the DFA. It then
uses LLVM to JIT the DFA.

Since regular expression derivatives permit the three basic Boolean
operations of disjunction (|), conjunction (&) and complement (!),
redgrep enables you to write very powerful regular expressions very
easily and guarantees to match them in linear time.

## Building

You must have GNU make, GNU bison and either GCC or Clang.

redgrep follows the "latest and greatest" in LLVM
development, so you should check out the source from
[Subversion](http://llvm.org/docs/GettingStarted.html#checkout-llvm-from-subversion)
or [Git](http://llvm.org/docs/GettingStarted.html#git-mirror),
then configure, build, check and install as per the
[instructions](http://llvm.org/docs/GettingStarted.html#getting-started-quickly-a-summary).
(Debian and Ubuntu users may prefer to install the [nightly
packages](http://llvm.org/apt/) instead.)

You should set the `LLVM_CONFIG` environment variable appropriately when
you run `make`.

## Contact

[redgrep@googlegroups.com](mailto:redgrep@googlegroups.com)

## Disclaimer

This is not an official Google product.
