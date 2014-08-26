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

#include "redgrep.h"

RED::RED(llvm::StringRef str) {
  redgrep::Exp exp;
  ok_ = redgrep::Parse(str, &exp);
  if (ok()) {
    redgrep::DFA dfa;
    redgrep::Compile(exp, &dfa);
    redgrep::Compile(dfa, &fun_);
  }
}

RED::~RED() {}

bool RED::FullMatch(llvm::StringRef str, const RED& re) {
  return redgrep::Match(re.fun_, str);
}
