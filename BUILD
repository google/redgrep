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

licenses(["notice"])

exports_files(["LICENSE"])

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

genrule(
    name = "parser",
    srcs = ["parser.yy"],
    outs = [
        "location.hh",
        "parser.tab.cc",
        "parser.tab.hh",
        "position.hh",
        "stack.hh",
    ],
    cmd = "bison -o $(location parser.tab.cc) $<",
)

cc_library(
    name = "library",
    srcs = [
        "redgrep.cc",
        "regexp.cc",
        ":parser",
    ],
    hdrs = [
        "redgrep.h",
        "regexp.h",
    ],
    copts = [
        "-funsigned-char",
    ],
    deps = [
        "@libutf_archive//:utf",
        "@local_config_redgrep//:llvm",
    ],
)

cc_test(
    name = "regexp_test",
    srcs = ["regexp_test.cc"],
    deps = [
        ":library",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "reddot",
    srcs = ["reddot.cc"],
    deps = [":library"],
)

cc_binary(
    name = "redasm",
    srcs = ["redasm.cc"],
    deps = [":library"],
)

cc_binary(
    name = "redgrep",
    srcs = ["redgrep_main.cc"],
    deps = [":library"],
)
