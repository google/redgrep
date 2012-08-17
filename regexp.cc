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

#include "regexp.h"

#include <stdlib.h>

#include <algorithm>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <utility>

#include "llvm/ADT/StringRef.h"
#include "parser.tab.hh"
#include "utf/utf.h"

namespace redgrep {

using std::list;
using std::make_pair;
using std::map;
using std::set;


Expression::Expression(Kind kind)
    : kind_(kind),
      data_(0),
      norm_(true) {}

Expression::Expression(Kind kind, Rune character)
    : kind_(kind),
      data_(character),
      norm_(true) {}

Expression::Expression(Kind kind, const set<Rune>& character_class)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new set<Rune>(character_class))),
      norm_(true) {}

Expression::Expression(Kind kind, const list<Exp>& subexpressions, bool norm)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new list<Exp>(subexpressions))),
      norm_(norm) {}

Expression::~Expression() {
  switch (kind()) {
    case kEmptySet:
    case kEmptyString:
    case kAnyCharacter:
      break;

    case kCharacter:
      break;

    case kCharacterClass:
      delete reinterpret_cast<set<Rune>*>(data());
      break;

    case kKleeneClosure:
    case kConcatenation:
    case kComplement:
    case kConjunction:
    case kDisjunction:
      delete reinterpret_cast<list<Exp>*>(data());
      break;
  }
}

Rune Expression::character() const {
  return data();
}

const set<Rune>& Expression::character_class() const {
  return *reinterpret_cast<set<Rune>*>(data());
}

const list<Exp>& Expression::subexpressions() const {
  return *reinterpret_cast<list<Exp>*>(data());
}

int Compare(Exp x, Exp y) {
  if (x->kind() < y->kind()) {
    return -1;
  }
  if (x->kind() > y->kind()) {
    return +1;
  }
  switch (x->kind()) {
    case kEmptySet:
    case kEmptyString:
    case kAnyCharacter:
      return 0;

    case kCharacter:
      if (x->character() < y->character()) {
        return -1;
      }
      if (x->character() > y->character()) {
        return +1;
      }
      return 0;

    case kCharacterClass:
      if (x->character_class() < y->character_class()) {
        return -1;
      }
      if (x->character_class() > y->character_class()) {
        return +1;
      }
      return 0;

    case kKleeneClosure:
    case kConcatenation:
    case kComplement:
    case kConjunction:
    case kDisjunction: {
      // Perform a lexicographical compare.
      list<Exp>::const_iterator xi = x->subexpressions().begin();
      list<Exp>::const_iterator yi = y->subexpressions().begin();
      while (xi != x->subexpressions().end() &&
             yi != y->subexpressions().end()) {
        int compare = Compare(*xi, *yi);
        if (compare != 0) {
          return compare;
        }
        ++xi;
        ++yi;
      }
      if (xi == x->subexpressions().end() &&
          yi != y->subexpressions().end()) {
        return -1;
      }
      if (xi != x->subexpressions().end() &&
          yi == y->subexpressions().end()) {
        return +1;
      }
      return 0;
    }
  }
  abort();
}

// TODO(junyer): EmptySet, EmptyString and AnyCharacter expressions could be
// singletons. Character expressions could be cached during parsing.

Exp EmptySet() {
  Exp exp(new Expression(kEmptySet));
  return exp;
}

Exp EmptyString() {
  Exp exp(new Expression(kEmptyString));
  return exp;
}

Exp AnyCharacter() {
  Exp exp(new Expression(kAnyCharacter));
  return exp;
}

Exp Character(Rune character) {
  Exp exp(new Expression(kCharacter, character));
  return exp;
}

Exp CharacterClass(const set<Rune>& character_class) {
  Exp exp(new Expression(kCharacterClass, character_class));
  return exp;
}

Exp KleeneClosure(const list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kKleeneClosure, subexpressions, norm));
  return exp;
}

Exp Concatenation(const list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kConcatenation, subexpressions, norm));
  return exp;
}

Exp Complement(const list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kComplement, subexpressions, norm));
  return exp;
}

Exp Conjunction(const list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kConjunction, subexpressions, norm));
  return exp;
}

Exp Disjunction(const list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kDisjunction, subexpressions, norm));
  return exp;
}

Exp Normalised(Exp exp) {
  if (exp->norm()) {
    return exp;
  }
  switch (exp->kind()) {
    case kEmptySet:
    case kEmptyString:
    case kAnyCharacter:
    case kCharacter:
    case kCharacterClass:
      return exp;

    case kKleeneClosure: {
      Exp sub = Normalised(exp->sub());
      // (r∗)∗ ≈ r∗
      if (sub->kind() == kKleeneClosure) {
        return sub;
      }
      // ∅∗ ≈ ε
      if (sub->kind() == kEmptySet) {
        return EmptyString();
      }
      // ε∗ ≈ ε
      if (sub->kind() == kEmptyString) {
        return EmptyString();
      }
      // .∗ ≈ ¬∅
      if (sub->kind() == kAnyCharacter) {
        return Complement({EmptySet()}, true);
      }
      return KleeneClosure({sub}, true);
    }

    case kConcatenation: {
      Exp head = exp->head();
      Exp tail = exp->tail();
      // (r · s) · t ≈ r · (s · t)
      head = Normalised(head);
      while (head->kind() == kConcatenation) {
        tail = Concatenation(head->tail(), tail);
        head = head->head();
      }
      tail = Normalised(tail);
      // ∅ · r ≈ ∅
      if (head->kind() == kEmptySet) {
        return head;
      }
      // r · ∅ ≈ ∅
      if (tail->kind() == kEmptySet) {
        return tail;
      }
      // ε · r ≈ r
      if (head->kind() == kEmptyString) {
        return tail;
      }
      // r · ε ≈ r
      if (tail->kind() == kEmptyString) {
        return head;
      }
      return Concatenation({head, tail}, true);
    }

    case kComplement: {
      Exp sub = Normalised(exp->sub());
      // ¬(¬r) ≈ r
      if (sub->kind() == kComplement) {
        return sub->sub();
      }
      return Complement({sub}, true);
    }

    case kConjunction: {
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Normalised(sub);
        // ∅ & r ≈ ∅
        // r & ∅ ≈ ∅
        if (sub->kind() == kEmptySet) {
          return sub;
        }
        // (r & s) & t ≈ r & (s & t)
        if (sub->kind() == exp->kind()) {
          list<Exp> copy = sub->subexpressions();
          subs.splice(subs.end(), copy);
        } else {
          subs.push_back(sub);
        }
      }
      // r & s ≈ s & r
      subs.sort();
      // r & r ≈ r
      subs.unique();
      // ¬∅ & r ≈ r
      // r & ¬∅ ≈ r
      subs.remove_if([&subs](Exp sub) -> bool {
        return (sub->kind() == kComplement &&
                sub->sub()->kind() == kEmptySet &&
                subs.size() > 1);
      });
      if (subs.size() == 1) {
        return subs.front();
      }
      return Conjunction(subs, true);
    }

    case kDisjunction: {
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Normalised(sub);
        // ¬∅ + r ≈ ¬∅
        // r + ¬∅ ≈ ¬∅
        if (sub->kind() == kComplement &&
            sub->sub()->kind() == kEmptySet) {
          return sub;
        }
        // (r + s) + t ≈ r + (s + t)
        if (sub->kind() == exp->kind()) {
          list<Exp> copy = sub->subexpressions();
          subs.splice(subs.end(), copy);
        } else {
          subs.push_back(sub);
        }
      }
      // r + s ≈ s + r
      subs.sort();
      // r + r ≈ r
      subs.unique();
      // ∅ + r ≈ r
      // r + ∅ ≈ r
      subs.remove_if([&subs](Exp sub) -> bool {
        return (sub->kind() == kEmptySet &&
                subs.size() > 1);
      });
      if (subs.size() == 1) {
        return subs.front();
      }
      return Disjunction(subs, true);
    }
  }
  abort();
}

bool IsNullable(Exp exp) {
  switch (exp->kind()) {
    case kEmptySet:
      // ν(∅) = ∅
      return false;

    case kEmptyString:
      // ν(ε) = ε
      return true;

    case kAnyCharacter:
      // ν(.) = ∅
      return false;

    case kCharacter:
      // ν(a) = ∅
      return false;

    case kCharacterClass:
      // ν(S) = ∅
      return false;

    case kKleeneClosure:
      // ν(r∗) = ε
      return true;

    case kConcatenation:
      // ν(r · s) = ν(r) & ν(s)
      // Non-lazy form:
      // return Conjunction(Nullability(exp->head()),
      //                    Nullability(exp->tail()));
      return IsNullable(exp->head()) && IsNullable(exp->tail());

    case kComplement:
      // ν(¬r) = ∅ if ν(r) = ε
      //         ε if ν(r) = ∅
      return !IsNullable(exp->sub());

    case kConjunction:
      // ν(r & s) = ν(r) & ν(s)
      for (Exp sub : exp->subexpressions()) {
        if (!IsNullable(sub)) {
          return false;
        }
      }
      return true;

    case kDisjunction:
      // ν(r + s) = ν(r) + ν(s)
      for (Exp sub : exp->subexpressions()) {
        if (IsNullable(sub)) {
          return true;
        }
      }
      return false;
  }
  abort();
}

Exp Derivative(Exp exp, Rune character) {
  switch (exp->kind()) {
    case kEmptySet:
      // ∂a∅ = ∅
      return EmptySet();

    case kEmptyString:
      // ∂aε = ∅
      return EmptySet();

    case kAnyCharacter:
      // ∂a. = ε
      return EmptyString();

    case kCharacter:
      // ∂aa = ε
      // ∂ab = ∅ for b ≠ a
      if (exp->character() == character) {
        return EmptyString();
      } else {
        return EmptySet();
      }

    case kCharacterClass:
      // ∂aS = ε if a ∈ S
      //       ∅ if a ∉ S
      if (exp->character_class().find(character) !=
          exp->character_class().end()) {
        return EmptyString();
      } else {
        return EmptySet();
      }

    case kKleeneClosure:
      // ∂a(r∗) = ∂ar · r∗
      return Concatenation(Derivative(exp->sub(), character),
                           exp);

    case kConcatenation:
      // ∂a(r · s) = ∂ar · s + ν(r) · ∂as
      // Non-lazy form:
      // return Disjunction(Concatenation(Derivative(exp->head(), character),
      //                                  exp->tail()),
      //                    Concatenation(Nullability(exp->head()),
      //                                  Derivative(exp->tail(), character)));
      if (IsNullable(exp->head())) {
        return Disjunction(Concatenation(Derivative(exp->head(), character),
                                         exp->tail()),
                           Derivative(exp->tail(), character));
      } else {
        return Concatenation(Derivative(exp->head(), character),
                             exp->tail());
      }

    case kComplement:
      // ∂a(¬r) = ¬(∂ar)
      return Complement(Derivative(exp->sub(), character));

    case kConjunction: {
      // ∂a(r & s) = ∂ar & ∂as
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Derivative(sub, character);
        subs.push_back(sub);
      }
      return Conjunction(subs, false);
    }

    case kDisjunction: {
      // ∂a(r + s) = ∂ar + ∂as
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Derivative(sub, character);
        subs.push_back(sub);
      }
      return Disjunction(subs, false);
    }
  }
  abort();
}

// Outputs the partitions obtained by intersecting the partitions in x and y.
// The first partition should be Σ-based. Any others should be ∅-based.
static void Intersection(const list<set<Rune>>& x,
                         const list<set<Rune>>& y,
                         list<set<Rune>>* z) {
  for (list<set<Rune>>::const_iterator xi = x.begin();
       xi != x.end();
       ++xi) {
    for (list<set<Rune>>::const_iterator yi = y.begin();
         yi != y.end();
         ++yi) {
      set<Rune> s;
      if (xi == x.begin()) {
        if (yi == y.begin()) {
          // *xi is Σ-based, *yi is Σ-based.
          std::set_union(xi->begin(), xi->end(),
                         yi->begin(), yi->end(),
                         std::inserter(s, s.end()));
          // s is Σ-based, so it can be empty.
          z->push_back(s);
        } else {
          // *xi is Σ-based, *yi is ∅-based.
          std::set_difference(yi->begin(), yi->end(),
                              xi->begin(), xi->end(),
                              std::inserter(s, s.end()));
          if (!s.empty()) {
            z->push_back(s);
          }
        }
      } else {
        if (yi == y.begin()) {
          // *xi is ∅-based, *yi is Σ-based.
          std::set_difference(xi->begin(), xi->end(),
                              yi->begin(), yi->end(),
                              std::inserter(s, s.end()));
          if (!s.empty()) {
            z->push_back(s);
          }
        } else {
          // *xi is ∅-based, *yi is ∅-based.
          std::set_intersection(yi->begin(), yi->end(),
                                xi->begin(), xi->end(),
                                std::inserter(s, s.end()));
          if (!s.empty()) {
            z->push_back(s);
          }
        }
      }
    }
  }
}

void Partitions(Exp exp, list<set<Rune>>* partitions) {
  partitions->clear();
  switch (exp->kind()) {
    case kEmptySet:
      // C(∅) = {Σ}
      partitions->push_back({});
      return;

    case kEmptyString:
      // C(ε) = {Σ}
      partitions->push_back({});
      return;

    case kAnyCharacter:
      // C(.) = {Σ}
      partitions->push_back({});
      return;

    case kCharacter:
      // C(a) = {a, Σ \ a}
      partitions->push_back({exp->character()});
      partitions->push_back({exp->character()});
      return;

    case kCharacterClass:
      // C(S) = {S, Σ \ S}
      partitions->push_back(exp->character_class());
      partitions->push_back(exp->character_class());
      return;

    case kKleeneClosure:
      // C(r∗) = C(r)
      Partitions(exp->sub(), partitions);
      return;

    case kConcatenation:
      // C(r · s) = C(r) ∧ C(s) if ν(r) = ε
      //            C(r)        if ν(r) = ∅
      if (IsNullable(exp->head())) {
        list<set<Rune>> x, y;
        Partitions(exp->head(), &x);
        Partitions(exp->tail(), &y);
        Intersection(x, y, partitions);
        return;
      } else {
        Partitions(exp->head(), partitions);
        return;
      }

    case kComplement:
      // C(¬r) = C(r)
      Partitions(exp->sub(), partitions);
      return;

    case kConjunction:
      // C(r & s) = C(r) ∧ C(s)
      for (Exp sub : exp->subexpressions()) {
        if (partitions->empty()) {
          Partitions(sub, partitions);
        } else {
          list<set<Rune>> x, y;
          std::swap(*partitions, x);
          Partitions(sub, &y);
          Intersection(x, y, partitions);
        }
      }
      return;

    case kDisjunction:
      // C(r + s) = C(r) ∧ C(s)
      for (Exp sub : exp->subexpressions()) {
        if (partitions->empty()) {
          Partitions(sub, partitions);
        } else {
          list<set<Rune>> x, y;
          std::swap(*partitions, x);
          Partitions(sub, &y);
          Intersection(x, y, partitions);
        }
      }
      return;
  }
  abort();
}

bool Parse(llvm::StringRef str, Exp* exp) {
  redgrep_yy::parser parser(&str, exp);
  return parser.parse() == 0;
}

bool Match(Exp exp, llvm::StringRef str) {
  for (;;) {
    Rune character;
    int len = charntorune(&character, str.data(), str.size());
    if (len == 0) {
      break;
    }
    str = str.substr(len);
    Exp der = Derivative(exp, character);
    der = Normalised(der);
    exp = der;
  }
  bool match = IsNullable(exp);
  return match;
}

int Compile(Exp exp, DFA* dfa) {
  dfa->transition_.clear();
  dfa->accepting_.clear();
  map<Exp, int> states;
  list<Exp> queue;
  queue.push_back(exp);
  while (!queue.empty()) {
    exp = queue.front();
    queue.pop_front();
    exp = Normalised(exp);
    auto state = states.insert(make_pair(exp, states.size()));
    int curr = state.first->second;
    dfa->accepting_[curr] = IsNullable(exp);
    list<set<Rune>> partitions;
    Partitions(exp, &partitions);
    int next0;
    for (list<set<Rune>>::const_iterator i = partitions.begin();
         i != partitions.end();
         ++i) {
      Rune character;
      if (i == partitions.begin()) {
        // *i is Σ-based. Use a character that it doesn't contain. ;)
        character = InvalidRune();
      } else {
        // *i is ∅-based. Use the first character that it contains.
        character = *i->begin();
      }
      Exp der = Derivative(exp, character);
      der = Normalised(der);
      auto state = states.insert(make_pair(der, states.size()));
      int next = state.first->second;
      if (state.second) {
        queue.push_back(der);
      }
      if (i == partitions.begin()) {
        // Set the "default" transition.
        dfa->transition_[make_pair(curr, character)] = next;
        next0 = next;
      } else if (next != next0) {
        for (Rune character : *i) {
          dfa->transition_[make_pair(curr, character)] = next;
        }
      }
    }
  }
  return states.size();
}

bool Match(const DFA& dfa, llvm::StringRef str) {
  int curr = 0;
  for (;;) {
    Rune character;
    int len = charntorune(&character, str.data(), str.size());
    if (len == 0) {
      break;
    }
    str = str.substr(len);
    auto transition = dfa.transition_.find(make_pair(curr, character));
    if (transition == dfa.transition_.end()) {
      // Get the "default" transition.
      character = InvalidRune();
      transition = dfa.transition_.find(make_pair(curr, character));
    }
    int next = transition->second;
    curr = next;
  }
  auto accepting = dfa.accepting_.find(curr);
  bool match = accepting->second;
  return match;
}

}  // namespace redgrep
