// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "llvm/ADT/StringRef.h"
#include "redgrep.h"

static constexpr char kUsage[] =
  "Usage: %s [OPTION]... REGEXP [FILE]...\n"
  "\n"
  "Options:\n"
  "\n"
  "  -v  select non-matching lines\n"
  "  -n  print line number with output lines\n"
  "  -H  print the file name for each match\n"
  "  -h  suppress the file name prefix on output\n"
  "\n"
  "Similar to the way in which find(1) lets you construct expressions,\n"
  "REGEXP may comprise multiple subexpressions as separate arguments:\n"
  "\n"
  "  [-e] EXPR       regular expression\n"
  "  ( EXPR )        grouping\n"
  "  ! EXPR          complement\n"
  "  -not EXPR\n"
  "  EXPR & EXPR     conjunction\n"
  "  EXPR -a EXPR\n"
  "  EXPR -and EXPR\n"
  "  EXPR | EXPR     disjunction\n"
  "  EXPR -o EXPR\n"
  "  EXPR -or EXPR\n"
  "\n"
  "EXPR may begin with `^' in order to anchor it to the beginning of the\n"
  "line and may end with `$' in order to anchor it to the end of the line.\n"
  "\n";

int main(int argc, char** argv) {
  // Parse options.
  bool opt_invert_match = false;
  bool opt_line_number = false;
  enum {
    kAlways, kMaybe, kNever,
  } opt_with_filename = kMaybe;
  bool escape = false;
  while (!escape) {
    int opt = getopt(argc, argv, "+vnHhe:");
    if (opt == -1) {
      break;
    }
    switch (opt) {
      case 'v':
        opt_invert_match = true;
        break;
      case 'n':
        opt_line_number = true;
        break;
      case 'H':
        opt_with_filename = kAlways;
        break;
      case 'h':
        opt_with_filename = kNever;
        break;
      case 'e':
        argv[--optind] = optarg;
        escape = true;
        break;
      default:
        // TODO(junyer): Move most of the usage text to `--help'.
        fprintf(stderr, kUsage, program_invocation_short_name);
        return 2;
    }
  }

  // Shift off parsed options.
  argc -= optind;
  argv += optind;

  // Build regular expression string.
  // TODO(junyer): Factor out for testing.
  std::string re_str;
  int parens = 0;
  bool complete = false;
  while (argc > 0) {
    std::string arg(*argv);
    if (!escape && arg == "-e") {
      if (complete) {
        re_str += "|";
      }
      escape = true;
      complete = false;
    } else if (!escape && arg == "(") {
      re_str += arg;
      ++parens;
    } else if (!escape && arg == ")") {
      re_str += arg;
      --parens;
      if (parens < 0) {
        errx(2, "unmatched right parenthesis");
      }
    } else if (!escape && (arg == "!" || arg == "-not")) {
      re_str += "!";
      complete = false;
    } else if (!escape && (arg == "&" || arg == "-a" || arg == "-and")) {
      re_str += "&";
      complete = false;
    } else if (!escape && (arg == "|" || arg == "-o" || arg == "-or")) {
      re_str += "|";
      complete = false;
    } else if (escape || !complete) {
      if (!arg.empty()) {
        if (arg.front() == '^') {
          arg = arg.substr(1);
        } else {
          arg = ".*" + arg;
        }
        if (arg.back() == '$') {
          arg.back() = '\n';
        } else {
          arg += ".*";
        }
        re_str += arg;
      }
      escape = false;
      complete = true;
    } else {
      break;
    }
    --argc;
    ++argv;
  }

  if (re_str.empty()) {
    errx(2, "regular expression not specified");
  }

  if (parens > 0) {
    errx(2, "unmatched left parenthesis");
  }

  if (!complete) {
    errx(2, "incomplete arguments");
  }

  if (opt_invert_match) {
    re_str = "!(" + re_str + ")";
  }

  RED re(re_str);
  if (!re.ok()) {
    errx(2, "parse error");
  }

  // Parse files.
  char const *const *files = argv;
  int nfiles = argc;
  if (nfiles == 0) {
    static char const *const kFiles[] = { "-", nullptr, };
    files = kFiles;
    nfiles = 1;
  }

  // Grep!
  bool matched = false;
  char* data = nullptr;
  size_t size = 0;
  for (int i = 0; i < nfiles; ++i) {
    bool file_is_stdin = (files[i][0] == '-' &&
                          files[i][1] == '\0');
    FILE* file = (file_is_stdin
                  // GNU grep lets you specify "-" more than once. To emulate
                  // this, we dup stdin here so that we don't close it later.
                  ? fdopen(dup(fileno(stdin)), "r")
                  : fopen(files[i], "r"));
    if (file == nullptr) {
      warn("%s", files[i]);
      continue;
    }
    for (int n = 1;; ++n) {
      ssize_t len = getline(&data, &size, file);
      if (len == -1) {
        break;
      }
      llvm::StringRef str(data, len);
      if (RED::FullMatch(str, re)) {
        matched = true;
        if (opt_with_filename == kAlways ||
            (opt_with_filename == kMaybe && nfiles > 1)) {
          printf("%s:", (file_is_stdin
                         ? "(standard input)"
                         : files[i]));
        }
        if (opt_line_number) {
          printf("%d:", n);
        }
        printf("%.*s", static_cast<int>(len), data);
      }
    }
    fclose(file);
  }
  free(data);

  // As per GNU grep, "The exit status is 0 if selected lines are found, and 1
  // if not found. If an error occurred the exit status is 2."
  return matched ? 0 : 1;
}
