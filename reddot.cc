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
#include <stdio.h>

#include "regexp.h"
#include "utf/utf.h"

static void EmitHeader() {
  printf("digraph reddot {\n");
}

static void EmitState(int curr, bool accepting) {
  printf("s%d", curr);
  if (accepting) {
    printf(" [style=filled fillcolor=grey]");
  }
  printf("\n");
}

static void EmitTransition(int curr, Rune character, int next) {
  char buf[UTFmax];
  int len;
  if (character == redgrep::InvalidRune()) {
    len = 0;
  } else {
    len = runetochar(buf, &character);
  }
  printf("s%d -> s%d [label=\"%.*s\"", curr, next, len, buf);
  if (character == redgrep::InvalidRune()) {
    printf(" style=dashed]");
  } else {
    printf("]");
  }
  printf("\n");
}

static void EmitFooter() {
  printf("}\n");
}

int main(int argc, char** argv) {
  const char* argv1 = argv[1];
  if (argv1 == nullptr) {
    errx(1, "regular expression not specified");
  }
  redgrep::Exp exp;
  if (!redgrep::Parse(argv1, &exp)) {
    errx(1, "parse error");
  }
  redgrep::DFA dfa;
  int states = redgrep::Compile(exp, &dfa);
  EmitHeader();
  for (int i = 0; i < states; ++i) {
    int curr = i;
    bool accepting = dfa.accepting_.find(curr)->second;
    EmitState(curr, accepting);
  }
  for (const auto& i : dfa.transition_) {
    int curr = i.first.first;
    Rune character = i.first.second;
    int next = i.second;
    EmitTransition(curr, character, next);
  }
  EmitFooter();
  return 0;
}
