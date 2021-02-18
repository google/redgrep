# Copyright 2021 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

def _which(repository_ctx, program):
    path = repository_ctx.which(program)
    if not path:
        fail("Finding %r failed" % (program,))
    return path

def _execute(repository_ctx, arguments):
    result = repository_ctx.execute(arguments)
    if result.return_code:
        fail("Executing %r failed: %r" % (arguments, result.stderr))
    return result

def _redgrep_configure_impl(repository_ctx):
    llvm_config = repository_ctx.attr.llvm_config
    if not llvm_config.startswith("/"):
        llvm_config = _which(repository_ctx, llvm_config)

    result = _execute(repository_ctx, [llvm_config, "--libdir"])
    libdir = result.stdout.strip()

    result = _execute(repository_ctx, [llvm_config, "--includedir"])
    includedir = result.stdout.strip()

    find = _which(repository_ctx, "find")

    result = _execute(repository_ctx, [find, "-L", includedir, "-type", "f"])
    hdrs = result.stdout.splitlines()

    repository_ctx.symlink("/", "ROOT")
    repository_ctx.file(
        "BUILD",
        content = """
cc_library(
    name = "llvm",
    srcs = ["ROOT" + {libdir} + "/libLLVM.so"],
    hdrs = ["ROOT" + hdr for hdr in {hdrs}],
    includes = ["ROOT" + {includedir}],
    visibility = ["//visibility:public"],
)
""".format(
            libdir = repr(libdir),
            includedir = repr(includedir),
            hdrs = repr(hdrs),
        ),
        executable = False,
    )

redgrep_configure = repository_rule(
    implementation = _redgrep_configure_impl,
    attrs = {
        "llvm_config": attr.string(
            default = "llvm-config-11",
        ),
    },
    configure = True,
)
