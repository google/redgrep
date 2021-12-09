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

workspace(name = "com_github_google_redgrep")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@com_github_google_redgrep//:redgrep_configure.bzl", "redgrep_configure")

http_archive(
    name = "rules_cc",
    strip_prefix = "rules_cc-main",
    urls = ["https://github.com/bazelbuild/rules_cc/archive/main.zip"],
)

http_archive(
    name = "com_google_googletest",
    strip_prefix = "googletest-main",
    urls = ["https://github.com/google/googletest/archive/main.zip"],
)

http_archive(
    name = "libutf_archive",
    build_file = "//third_party:libutf.BUILD",
    strip_prefix = "libutf-master",
    urls = ["https://github.com/cls/libutf/archive/master.zip"],
)

redgrep_configure(name = "local_config_redgrep")
