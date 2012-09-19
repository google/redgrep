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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "llvm/ADT/StringRef.h"
#include "redgrep.h"

int main(int argc, char** argv) {
  // Parse options.
  bool invert_match = false;
  for (;;) {
    int opt = getopt(argc, argv, "v");
    if (opt == -1) {
      break;
    }
    switch (opt) {
      case 'v':
        invert_match = true;
        break;
      default:
        errx(1, "Usage: %s [OPTION]... REGEXP [FILE]...", argv[0]);
    }
  }

  // Parse regular expression.
  if (optind == argc) {
    errx(1, "regular expression not specified");
  }
  RED re(argv[optind++]);
  if (!re.ok()) {
    errx(1, "parse error");
  }

  // Parse files.
  char const *const *files = argv + optind;
  int nfiles = argc - optind;
  if (nfiles == 0) {
    static char const *const kFiles[] = { "-", nullptr, };
    files = kFiles;
    nfiles = 1;
  }

  // Grep!
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
    for (;;) {
      ssize_t len = getline(&data, &size, file);
      if (len == -1) {
        break;
      }
      llvm::StringRef str(data, len);
      bool match = RED::FullMatch(str, re);
      if (invert_match) {
        match = !match;
      }
      if (match) {
        if (nfiles >= 2) {
          printf("%s:", (file_is_stdin
                         ? "(standard input)"
                         : files[i]));
        }
        printf("%.*s", static_cast<int>(len), data);
      }
    }
    fclose(file);
  }
  free(data);

  return 0;
}
