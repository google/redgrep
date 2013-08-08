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
#include <string.h>
#include <unistd.h>

#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "regexp.h"

using std::list;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::vector;

static void EmitHeader() {
  printf("digraph reddot {\n");
}

static void EmitState(int curr, const char* fillcolor) {
  printf("s%d", curr);
  printf(" [style=filled fillcolor=%s]", fillcolor);
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

static void EmitEpsilon(int curr, int next, const set<int>& tags) {
  printf("s%d -> s%d [label=\"", curr, next);
  for (int tag : tags) {
    printf("%d ", tag);
  }
  printf("\" style=dotted]");
  printf("\n");
}

static void EmitFooter() {
  printf("}\n");
}

inline void HandleImpl(int nstates, const redgrep::FA& fa) {
  EmitHeader();
  for (int i = 0; i < nstates; ++i) {
    int curr = i;
    if (fa.IsErrorState(curr)) {
      EmitState(curr, "red");
    } else if (fa.IsAcceptingState(curr)) {
      EmitState(curr, "green");
    } else if (fa.IsGlueState(curr)) {
      EmitState(curr, "grey");
    } else {
      EmitState(curr, "white");
    }
  }
  map<pair<int, int>, list<pair<int, int>>> transition_map;
  for (const auto& i : fa.transition_) {
    int curr = i.first.first;
    int byte = i.first.second;
    int next = i.second;
    if (fa.IsErrorState(next)) {
      continue;
    } else if (byte == -1) {
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
  for (const auto& i : fa.epsilon_) {
    int curr = i.first;
    int next = i.second.first;
    const set<int>& tags = i.second.second;
    EmitEpsilon(curr, next, tags);
  }
  EmitFooter();
}

static void HandleDFA(const char* str) {
  redgrep::Exp exp;
  redgrep::DFA dfa;
  if (!redgrep::Parse(str, &exp)) {
    errx(1, "parse error");
  }
  int nstates = redgrep::Compile(exp, &dfa);
  HandleImpl(nstates, dfa);
}

static void HandleTNFA(const char* str) {
  redgrep::Exp exp;
  redgrep::TNFA tnfa;
  if (!redgrep::Parse(str, &exp, &tnfa.modes_, &tnfa.groups_)) {
    errx(1, "parse error");
  }
  int nstates = redgrep::Compile(exp, &tnfa);
  HandleImpl(nstates, tnfa);
}

int main(int argc, char** argv) {
  // Parse options.
  enum {
    kDFA, kTNFA, kTDFA,
  } mode = kDFA;
  for (;;) {
    int opt = getopt(argc, argv, "m:");
    if (opt == -1) {
      break;
    }
    switch (opt) {
      case 'm':
        if (strcmp(optarg, "dfa") == 0) {
          mode = kDFA;
        } else if (strcmp(optarg, "tnfa") == 0) {
          mode = kTNFA;
        } else if (strcmp(optarg, "tdfa") == 0) {
          mode = kTDFA;
        } else {
          errx(1, "invalid mode");
        }
        break;
      default:
        errx(1, "Usage: %s [OPTION]... REGEXP", argv[0]);
    }
  }

  if (optind == argc) {
    errx(1, "regular expression not specified");
  }

  switch (mode) {
    case kDFA:
      HandleDFA(argv[optind++]);
      break;
    case kTNFA:
      HandleTNFA(argv[optind++]);
      break;
    case kTDFA:
    default:
      errx(1, "not implemented");
  }

  return 0;
}
