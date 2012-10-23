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

#include <list>
#include <map>
#include <utility>

#include "regexp.h"

using std::list;
using std::make_pair;
using std::map;
using std::pair;

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

static void EmitTransition(int curr, int byte, int next) {
  printf("s%d -> s%d [label=\"", curr, next);
  if (byte == -1) {
    printf("\" style=dashed]");
  } else {
    printf("%02X\"]", byte);
  }
  printf("\n");
}

static void EmitTransition(int curr, int begin, int end, int next) {
  printf("s%d -> s%d [label=\"", curr, next);
  printf("%02X-%02X\"]", begin, end);
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
  int nstates = redgrep::Compile(exp, &dfa);
  EmitHeader();
  for (int i = 0; i < nstates; ++i) {
    int curr = i;
    bool accepting = dfa.accepting_.find(curr)->second;
    EmitState(curr, accepting);
  }
  map<pair<int, int>, list<pair<int, int>>> transition_map;
  for (const auto& i : dfa.transition_) {
    int curr = i.first.first;
    int byte = i.first.second;
    int next = i.second;
    if (byte == -1) {
      EmitTransition(curr, byte, next);
    } else {
      auto& range_list = transition_map[make_pair(curr, next)];
      if (range_list.empty() ||
          range_list.back().second + 1 != byte) {
        range_list.push_back(make_pair(byte, byte));
      } else {
        range_list.back().second = byte;
      }
    }
  }
  for (const auto& i : transition_map) {
    int curr = i.first.first;
    int next = i.first.second;
    const auto& range_list = i.second;
    for (const auto& j : range_list) {
      int begin = j.first;
      int end = j.second;
      if (begin == end) {
        EmitTransition(curr, begin, next);
      } else {
        EmitTransition(curr, begin, end, next);
      }
    }
  }
  EmitFooter();
  return 0;
}
