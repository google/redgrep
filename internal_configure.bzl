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

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _which(repository_ctx, program):
    path = repository_ctx.which(program)
    if not path:
        fail("Finding %r failed" % (program,))
    return path

def _execute(repository_ctx, arguments):
    result = repository_ctx.execute(arguments)
    if result.return_code:
        fail("Executing %r failed: %r" % (arguments, result.stderr))
    return result.stdout.strip()

def _llvm_repository_impl(repository_ctx):
    llvm_config = _which(repository_ctx, "llvm-config-16")
    libfiles = _execute(repository_ctx, [llvm_config, "--libfiles"])
    includedir = _execute(repository_ctx, [llvm_config, "--includedir"])
    repository_ctx.symlink("/", "ROOT")
    repository_ctx.file(
        "BUILD.bazel",
        content = """\
cc_library(
    name = "llvm",
    srcs = ["ROOT" + {libfiles}],
    hdrs = glob(["ROOT" + {includedir} + "/**/*.*"]),
    includes = ["ROOT" + {includedir}],
    visibility = ["//visibility:public"],
)
""".format(
            libfiles = repr(libfiles),
            includedir = repr(includedir),
        ),
    )

_llvm_repository = repository_rule(implementation = _llvm_repository_impl)

def _internal_configure_extension_impl(module_ctx):
    http_archive(
        name = "libutf",
        build_file = "//:libutf-BUILD.bazel",
        strip_prefix = "libutf-master",
        urls = ["https://github.com/cls/libutf/archive/master.zip"],
    )
    _llvm_repository(
        name = "local_config_llvm",
    )

internal_configure_extension = module_extension(implementation = _internal_configure_extension_impl)
