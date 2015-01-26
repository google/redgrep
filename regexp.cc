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
#include <string.h>

#include <bitset>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "parser.tab.hh"
#include "utf/utf.h"

namespace redgrep {

using std::bitset;
using std::list;
using std::make_pair;
using std::make_tuple;
using std::map;
using std::pair;
using std::set;
using std::tuple;
using std::vector;

#define TO_INTPTR_T(ptr) reinterpret_cast<intptr_t>(ptr)

Expression::Expression(Kind kind)
    : kind_(kind),
      data_(0),
      norm_(true) {}

Expression::Expression(Kind kind, const tuple<int, Exp, Mode, bool>& group)
    : kind_(kind),
      data_(TO_INTPTR_T((new tuple<int, Exp, Mode, bool>(group)))),
      norm_(false) {}

Expression::Expression(Kind kind, int byte)
    : kind_(kind),
      data_(byte),
      norm_(true) {}

Expression::Expression(Kind kind, const pair<int, int>& byte_range)
    : kind_(kind),
      data_(TO_INTPTR_T((new pair<int, int>(byte_range)))),
      norm_(true) {}

Expression::Expression(Kind kind, const list<Exp>& subexpressions, bool norm)
    : kind_(kind),
      data_(TO_INTPTR_T((new list<Exp>(subexpressions)))),
      norm_(norm) {}

Expression::Expression(Kind kind, const pair<set<Rune>, bool>& character_class)
    : kind_(kind),
      data_(TO_INTPTR_T((new pair<set<Rune>, bool>(character_class)))),
      norm_(false) {}

Expression::Expression(Kind kind, const tuple<Exp, int, int>& quantifier)
    : kind_(kind),
      data_(TO_INTPTR_T((new tuple<Exp, int, int>(quantifier)))),
      norm_(false) {}

Expression::~Expression() {
  switch (kind()) {
    case kEmptySet:
    case kEmptyString:
      break;

    case kGroup:
      delete reinterpret_cast<tuple<int, Exp, Mode, bool>*>(data());
      break;

    case kAnyByte:
      break;

    case kByte:
      break;

    case kByteRange:
      delete reinterpret_cast<pair<int, int>*>(data());
      break;

    case kKleeneClosure:
    case kConcatenation:
    case kComplement:
    case kConjunction:
    case kDisjunction:
      delete reinterpret_cast<list<Exp>*>(data());
      break;

    case kCharacterClass:
      delete reinterpret_cast<pair<set<Rune>, bool>*>(data());
      break;

    case kQuantifier:
      delete reinterpret_cast<tuple<Exp, int, int>*>(data());
      break;
  }
}

const tuple<int, Exp, Mode, bool>& Expression::group() const {
  return *reinterpret_cast<tuple<int, Exp, Mode, bool>*>(data());
}

int Expression::byte() const {
  return data();
}

const pair<int, int>& Expression::byte_range() const {
  return *reinterpret_cast<pair<int, int>*>(data());
}

const list<Exp>& Expression::subexpressions() const {
  return *reinterpret_cast<list<Exp>*>(data());
}

const pair<set<Rune>, bool>& Expression::character_class() const {
  return *reinterpret_cast<pair<set<Rune>, bool>*>(data());
}

const tuple<Exp, int, int>& Expression::quantifier() const {
  return *reinterpret_cast<tuple<Exp, int, int>*>(data());
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
      return 0;

    case kGroup:
      if (x->group() < y->group()) {
        return -1;
      }
      if (x->group() > y->group()) {
        return +1;
      }
      return 0;

    case kAnyByte:
      return 0;

    case kByte:
      if (x->byte() < y->byte()) {
        return -1;
      }
      if (x->byte() > y->byte()) {
        return +1;
      }
      return 0;

    case kByteRange:
      if (x->byte_range() < y->byte_range()) {
        return -1;
      }
      if (x->byte_range() > y->byte_range()) {
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

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

Exp EmptySet() {
  Exp exp(new Expression(kEmptySet));
  return exp;
}

Exp EmptyString() {
  Exp exp(new Expression(kEmptyString));
  return exp;
}

Exp Group(const tuple<int, Exp, Mode, bool>& group) {
  Exp exp(new Expression(kGroup, group));
  return exp;
}

Exp AnyByte() {
  Exp exp(new Expression(kAnyByte));
  return exp;
}

Exp Byte(int byte) {
  Exp exp(new Expression(kByte, byte));
  return exp;
}

Exp ByteRange(const pair<int, int>& byte_range) {
  Exp exp(new Expression(kByteRange, byte_range));
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

Exp CharacterClass(const pair<set<Rune>, bool>& character_class) {
  Exp exp(new Expression(kCharacterClass, character_class));
  return exp;
}

Exp Quantifier(const tuple<Exp, int, int>& quantifier) {
  Exp exp(new Expression(kQuantifier, quantifier));
  return exp;
}

Exp AnyCharacter() {
  Exp b1 = ByteRange(0x00, 0x7F);  // 0xxxxxxx
  Exp bx = ByteRange(0x80, 0xBF);  // 10xxxxxx
  Exp b2 = ByteRange(0xC0, 0xDF);  // 110xxxxx
  Exp b3 = ByteRange(0xE0, 0xEF);  // 1110xxxx
  Exp b4 = ByteRange(0xF0, 0xF7);  // 11110xxx
  return Disjunction(b1,
                     Concatenation(b2, bx),
                     Concatenation(b3, bx, bx),
                     Concatenation(b4, bx, bx, bx));
}

Exp Character(Rune character) {
  char buf[4];
  int len = runetochar(buf, &character);
  switch (len) {
    case 1:
      return Byte(buf[0]);
    case 2:
      return Concatenation(Byte(buf[0]),
                           Byte(buf[1]));
    case 3:
      return Concatenation(Byte(buf[0]),
                           Byte(buf[1]),
                           Byte(buf[2]));
    case 4:
      return Concatenation(Byte(buf[0]),
                           Byte(buf[1]),
                           Byte(buf[2]),
                           Byte(buf[3]));
    default:
      break;
  }
  abort();
}

Exp Normalised(Exp exp) {
  if (exp->norm()) {
    return exp;
  }
  switch (exp->kind()) {
    case kEmptySet:
    case kEmptyString:
      return exp;

    case kGroup: {
      int num; Exp sub; Mode mode; bool capture;
      std::tie(num, sub, mode, capture) = exp->group();
      sub = Normalised(sub);
      if (sub->kind() == kEmptySet) {
        return EmptySet();
      }
      if (sub->kind() == kEmptyString) {
        return EmptyString();
      }
      return Group(num, sub, mode, capture);
    }

    case kAnyByte:
    case kByte:
    case kByteRange:
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
      // \C∗ ≈ ¬∅
      if (sub->kind() == kAnyByte) {
        return Complement({EmptySet()}, true);
      }
      // .∗ ≈ ¬∅
      // This is not strictly correct, but it is not the regular expression
      // engine's job to ensure that the input is structurally valid UTF-8.
      if (sub == AnyCharacter()) {
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
        if (sub->kind() == kConjunction) {
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
        if (sub->kind() == kDisjunction) {
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

    case kCharacterClass:
    case kQuantifier:
      break;
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

    case kGroup:
      return IsNullable(std::get<1>(exp->group()));

    case kAnyByte:
      // ν(\C) = ∅
      return false;

    case kByte:
      // ν(a) = ∅
      return false;

    case kByteRange:
      // ν(S) = ∅
      return false;

    case kKleeneClosure:
      // ν(r∗) = ε
      return true;

    case kConcatenation:
      // ν(r · s) = ν(r) & ν(s)
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

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

Exp Derivative(Exp exp, int byte) {
  switch (exp->kind()) {
    case kEmptySet:
      // ∂a∅ = ∅
      return EmptySet();

    case kEmptyString:
      // ∂aε = ∅
      return EmptySet();

    case kGroup:
      // This should never happen.
      break;

    case kAnyByte:
      // ∂a\C = ε
      return EmptyString();

    case kByte:
      // ∂aa = ε
      // ∂ab = ∅ for b ≠ a
      if (exp->byte() == byte) {
        return EmptyString();
      } else {
        return EmptySet();
      }

    case kByteRange:
      // ∂aS = ε if a ∈ S
      //       ∅ if a ∉ S
      if (exp->byte_range().first <= byte &&
          byte <= exp->byte_range().second) {
        return EmptyString();
      } else {
        return EmptySet();
      }

    case kKleeneClosure:
      // ∂a(r∗) = ∂ar · r∗
      return Concatenation(Derivative(exp->sub(), byte),
                           exp);

    case kConcatenation:
      // ∂a(r · s) = ∂ar · s + ν(r) · ∂as
      if (IsNullable(exp->head())) {
        return Disjunction(Concatenation(Derivative(exp->head(), byte),
                                         exp->tail()),
                           Derivative(exp->tail(), byte));
      } else {
        return Concatenation(Derivative(exp->head(), byte),
                             exp->tail());
      }

    case kComplement:
      // ∂a(¬r) = ¬(∂ar)
      return Complement(Derivative(exp->sub(), byte));

    case kConjunction: {
      // ∂a(r & s) = ∂ar & ∂as
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Derivative(sub, byte);
        subs.push_back(sub);
      }
      return Conjunction(subs, false);
    }

    case kDisjunction: {
      // ∂a(r + s) = ∂ar + ∂as
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Derivative(sub, byte);
        subs.push_back(sub);
      }
      return Disjunction(subs, false);
    }

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

Outer Denormalised(Exp exp) {
  Outer outer(new OuterSet);
  exp = Normalised(exp);
  if (exp->kind() != kDisjunction) {
    exp = Disjunction({exp}, false);
  }
  for (Exp sub : exp->subexpressions()) {
    if (sub->kind() != kConjunction) {
      sub = Conjunction({sub}, false);
    }
    outer->push_back(make_pair(sub, Bindings({})));
  }
  return outer;
}

Outer PartialConcatenation(Outer x, Exp y, const Bindings& initial) {
  // We mutate x as an optimisation.
  for (auto& xi : *x) {
    list<Exp> subs;
    for (Exp sub : xi.first->subexpressions()) {
      sub = Concatenation(sub, y);
      subs.push_back(sub);
    }
    xi.first = Conjunction(subs, false);
    xi.second.insert(xi.second.begin(), initial.begin(), initial.end());
  }
  return x;
}

Outer PartialComplement(Outer x) {
  Outer outer(nullptr);
  for (const auto& xi : *x) {
    Outer tmp(new OuterSet);
    for (Exp sub : xi.first->subexpressions()) {
      sub = Complement(sub);
      sub = Conjunction({sub}, false);
      tmp->push_back(make_pair(sub, Bindings({})));
    }
    if (outer == nullptr) {
      outer = std::move(tmp);
    } else {
      outer = PartialConjunction(std::move(outer), std::move(tmp));
    }
  }
  return outer;
}

Outer PartialConjunction(Outer x, Outer y) {
  Outer outer(new OuterSet);
  for (const auto& xi : *x) {
    for (const auto& yi : *y) {
      Exp sub = Conjunction(xi.first, yi.first);
      Bindings bindings;
      bindings.insert(bindings.end(), xi.second.begin(), xi.second.end());
      bindings.insert(bindings.end(), yi.second.begin(), yi.second.end());
      outer->push_back(make_pair(sub, bindings));
    }
  }
  return outer;
}

Outer PartialDisjunction(Outer x, Outer y) {
  // We mutate x as an optimisation.
  x->insert(x->end(), y->begin(), y->end());
  return x;
}

// Computes the cancel Bindings for exp.
static void CancelBindings(Exp exp, Bindings* bindings) {
  switch (exp->kind()) {
    case kEmptySet:
    case kEmptyString:
      return;

    case kGroup: {
      int num; Exp sub;
      std::tie(num, sub, std::ignore, std::ignore) = exp->group();
      bindings->push_back(make_pair(num, kCancel));
      CancelBindings(sub, bindings);
      return;
    }

    case kAnyByte:
    case kByte:
    case kByteRange:
      return;

    case kKleeneClosure:
      CancelBindings(exp->sub(), bindings);
      return;

    case kConcatenation:
      CancelBindings(exp->head(), bindings);
      CancelBindings(exp->tail(), bindings);
      return;

    case kComplement:
      return;

    case kConjunction:
    case kDisjunction:
      for (Exp sub : exp->subexpressions()) {
        CancelBindings(sub, bindings);
      }
      return;

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

// Computes the epsilon Bindings for exp.
static void EpsilonBindings(Exp exp, Bindings* bindings) {
  switch (exp->kind()) {
    case kEmptySet:
    case kEmptyString:
      return;

    case kGroup: {
      int num; Exp sub;
      std::tie(num, sub, std::ignore, std::ignore) = exp->group();
      bindings->push_back(make_pair(num, kEpsilon));
      EpsilonBindings(sub, bindings);
      return;
    }

    case kAnyByte:
    case kByte:
    case kByteRange:
      return;

    case kKleeneClosure:
      if (IsNullable(exp->sub())) {
        EpsilonBindings(exp->sub(), bindings);
      }
      return;

    case kConcatenation:
      EpsilonBindings(exp->head(), bindings);
      EpsilonBindings(exp->tail(), bindings);
      return;

    case kComplement:
      return;

    case kConjunction:
      for (Exp sub : exp->subexpressions()) {
        EpsilonBindings(sub, bindings);
      }
      return;

    case kDisjunction:
      for (Exp sub : exp->subexpressions()) {
        if (IsNullable(sub)) {
          EpsilonBindings(sub, bindings);
          return;
        }
      }
      return;

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

Outer Partial(Exp exp, int byte) {
  switch (exp->kind()) {
    case kEmptySet:
      // ∂a∅ = ∅
      return Denormalised(EmptySet());

    case kEmptyString:
      // ∂aε = ∅
      return Denormalised(EmptySet());

    case kGroup: {
      int num; Exp sub; Mode mode; bool capture;
      std::tie(num, sub, mode, capture) = exp->group();
      Outer outer = Partial(sub, byte);
      for (auto& i : *outer) {
        i.first = Group(num, i.first, mode, capture);
        i.first = Conjunction({i.first}, false);
        i.second.push_back(make_pair(num, kAppend));
      }
      return outer;
    }

    case kAnyByte:
      // ∂a\C = ε
      return Denormalised(EmptyString());

    case kByte:
      // ∂aa = ε
      // ∂ab = ∅ for b ≠ a
      if (exp->byte() == byte) {
        return Denormalised(EmptyString());
      } else {
        return Denormalised(EmptySet());
      }

    case kByteRange:
      // ∂aS = ε if a ∈ S
      //       ∅ if a ∉ S
      if (exp->byte_range().first <= byte &&
          byte <= exp->byte_range().second) {
        return Denormalised(EmptyString());
      } else {
        return Denormalised(EmptySet());
      }

    case kKleeneClosure: {
      // ∂a(r∗) = ∂ar · r∗
      Bindings cancel;
      CancelBindings(exp->sub(), &cancel);
      return PartialConcatenation(Partial(exp->sub(), byte),
                                  exp,
                                  cancel);
    }

    case kConcatenation:
      // ∂a(r · s) = ∂ar · s + ν(r) · ∂as
      if (IsNullable(exp->head())) {
        Bindings epsilon;
        EpsilonBindings(exp->head(), &epsilon);
        return PartialDisjunction(
            PartialConcatenation(Partial(exp->head(), byte),
                                 exp->tail(),
                                 Bindings({})),
            PartialConcatenation(Partial(exp->tail(), byte),
                                 EmptyString(),
                                 epsilon));
      } else {
        return PartialConcatenation(Partial(exp->head(), byte),
                                    exp->tail(),
                                    Bindings({}));
      }

    case kComplement:
      // ∂a(¬r) = ¬(∂ar)
      return PartialComplement(Partial(exp->sub(), byte));

    case kConjunction: {
      // ∂a(r & s) = ∂ar & ∂as
      Outer outer(nullptr);
      for (Exp sub : exp->subexpressions()) {
        Outer tmp = Partial(sub, byte);
        if (outer == nullptr) {
          outer = std::move(tmp);
        } else {
          outer = PartialConjunction(std::move(outer), std::move(tmp));
        }
      }
      return outer;
    }

    case kDisjunction: {
      // ∂a(r + s) = ∂ar + ∂as
      Outer outer(nullptr);
      for (Exp sub : exp->subexpressions()) {
        Outer tmp = Partial(sub, byte);
        if (outer == nullptr) {
          outer = std::move(tmp);
        } else {
          outer = PartialDisjunction(std::move(outer), std::move(tmp));
        }
      }
      return outer;
    }

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

// Outputs the partitions obtained by intersecting the partitions in x and y.
// The first partition should be Σ-based. Any others should be ∅-based.
static void Intersection(const list<bitset<256>>& x,
                         const list<bitset<256>>& y,
                         list<bitset<256>>* z) {
  for (list<bitset<256>>::const_iterator xi = x.begin();
       xi != x.end();
       ++xi) {
    for (list<bitset<256>>::const_iterator yi = y.begin();
         yi != y.end();
         ++yi) {
      bitset<256> bs;
      if (xi == x.begin()) {
        if (yi == y.begin()) {
          // Perform set union: *xi is Σ-based, *yi is Σ-based.
          bs = *xi | *yi;
          // bs is Σ-based, so it can be empty.
          z->push_back(bs);
        } else {
          // Perform set difference: *xi is Σ-based, *yi is ∅-based.
          bs = *yi & ~*xi;
          if (bs.any()) {
            z->push_back(bs);
          }
        }
      } else {
        if (yi == y.begin()) {
          // Perform set difference: *xi is ∅-based, *yi is Σ-based.
          bs = *xi & ~*yi;
          if (bs.any()) {
            z->push_back(bs);
          }
        } else {
          // Perform set intersection: *xi is ∅-based, *yi is ∅-based.
          bs = *yi & *xi;
          if (bs.any()) {
            z->push_back(bs);
          }
        }
      }
    }
  }
}

void Partitions(Exp exp, list<bitset<256>>* partitions) {
  switch (exp->kind()) {
    case kEmptySet:
      // C(∅) = {Σ}
      partitions->push_back({});
      return;

    case kEmptyString:
      // C(ε) = {Σ}
      partitions->push_back({});
      return;

    case kGroup:
      Partitions(std::get<1>(exp->group()), partitions);
      return;

    case kAnyByte:
      // C(\C) = {Σ}
      partitions->push_back({});
      return;

    case kByte: {
      // C(a) = {Σ \ a, a}
      bitset<256> bs;
      bs.set(exp->byte());
      partitions->push_back(bs);
      partitions->push_back(bs);
      return;
    }

    case kByteRange: {
      // C(S) = {Σ \ S, S}
      bitset<256> bs;
      for (int i = exp->byte_range().first;
           i <= exp->byte_range().second;
           ++i) {
        bs.set(i);
      }
      partitions->push_back(bs);
      partitions->push_back(bs);
      return;
    }

    case kKleeneClosure:
      // C(r∗) = C(r)
      Partitions(exp->sub(), partitions);
      return;

    case kConcatenation:
      // C(r · s) = C(r) ∧ C(s) if ν(r) = ε
      //            C(r)        if ν(r) = ∅
      if (IsNullable(exp->head())) {
        list<bitset<256>> x, y;
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
          list<bitset<256>> x, y;
          partitions->swap(x);
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
          list<bitset<256>> x, y;
          partitions->swap(x);
          Partitions(sub, &y);
          Intersection(x, y, partitions);
        }
      }
      return;

    case kCharacterClass:
    case kQuantifier:
      break;
  }
  abort();
}

// A simple framework for implementing the post-parse rewrites.
class Walker {
 public:
  Walker() {}
  virtual ~Walker() {}

  virtual Exp WalkGroup(Exp exp) {
    int num; Exp sub; Mode mode; bool capture;
    std::tie(num, sub, mode, capture) = exp->group();
    sub = Walk(sub);
    return Group(num, sub, mode, capture);
  }

  virtual Exp WalkKleeneClosure(Exp exp) {
    Exp sub = Walk(exp->sub());
    return KleeneClosure(sub);
  }

  virtual Exp WalkConcatenation(Exp exp) {
    Exp head = Walk(exp->head());
    Exp tail = Walk(exp->tail());
    return Concatenation(head, tail);
  }

  virtual Exp WalkComplement(Exp exp) {
    Exp sub = Walk(exp->sub());
    return Complement(sub);
  }

  virtual Exp WalkConjunction(Exp exp) {
    list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = Walk(sub);
      subs.push_back(sub);
    }
    return Conjunction(subs, false);
  }

  virtual Exp WalkDisjunction(Exp exp) {
    list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = Walk(sub);
      subs.push_back(sub);
    }
    return Disjunction(subs, false);
  }

  virtual Exp WalkCharacterClass(Exp exp) {
    return exp;
  }

  virtual Exp WalkQuantifier(Exp exp) {
    Exp sub; int min; int max;
    std::tie(sub, min, max) = exp->quantifier();
    sub = Walk(sub);
    return Quantifier(sub, min, max);
  }

  Exp Walk(Exp exp) {
    switch (exp->kind()) {
      case kEmptySet:
      case kEmptyString:
        return exp;

      case kGroup:
        return WalkGroup(exp);

      case kAnyByte:
      case kByte:
      case kByteRange:
        return exp;

      case kKleeneClosure:
        return WalkKleeneClosure(exp);

      case kConcatenation:
        return WalkConcatenation(exp);

      case kComplement:
        return WalkComplement(exp);

      case kConjunction:
        return WalkConjunction(exp);

      case kDisjunction:
        return WalkDisjunction(exp);

      case kCharacterClass:
        return WalkCharacterClass(exp);

      case kQuantifier:
        return WalkQuantifier(exp);
    }
    abort();
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(Walker);
  Walker(const Walker&) = delete;
  void operator=(const Walker&) = delete;
};

class FlattenConjunctionsAndDisjunctions : public Walker {
 public:
  FlattenConjunctionsAndDisjunctions() {}
  ~FlattenConjunctionsAndDisjunctions() override {}

  inline void FlattenImpl(Exp exp, list<Exp>* subs) {
    Kind kind = exp->kind();
    // In most cases, exp is a left-skewed binary tree.
    while (exp->kind() == kind &&
           exp->subexpressions().size() == 2) {
      subs->push_front(exp->tail());
      exp = exp->head();
    }
    if (exp->kind() == kind) {
      list<Exp> copy = exp->subexpressions();
      subs->splice(subs->begin(), copy);
    } else {
      subs->push_front(exp);
    }
    list<Exp>::iterator i = subs->begin();
    while (i != subs->end()) {
      Exp sub = *i;
      sub = Walk(sub);
      if (sub->kind() == kind) {
        list<Exp> copy = sub->subexpressions();
        subs->splice(i, copy);
        i = subs->erase(i);
      } else {
        *i = sub;
        ++i;
      }
    }
  }

  Exp WalkConjunction(Exp exp) override {
    list<Exp> subs;
    FlattenImpl(exp, &subs);
    return Conjunction(subs, false);
  }

  Exp WalkDisjunction(Exp exp) override {
    list<Exp> subs;
    FlattenImpl(exp, &subs);
    return Disjunction(subs, false);
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(FlattenConjunctionsAndDisjunctions);
  FlattenConjunctionsAndDisjunctions(const FlattenConjunctionsAndDisjunctions&) = delete;
  void operator=(const FlattenConjunctionsAndDisjunctions&) = delete;
};

class StripGroups : public Walker {
 public:
  StripGroups() {}
  ~StripGroups() override {}

  Exp WalkGroup(Exp exp) override {
    Exp sub = Walk(std::get<1>(exp->group()));
    return sub;
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(StripGroups);
  StripGroups(const StripGroups&) = delete;
  void operator=(const StripGroups&) = delete;
};

class ApplyGroups : public Walker {
 public:
  ApplyGroups() {}
  ~ApplyGroups() override {}

  Exp WalkComplement(Exp exp) override {
    Exp sub = Walk(exp->sub());
    sub = Complement(sub);
    return Group(-1, sub, kMaximal, false);
  }

  Exp WalkDisjunction(Exp exp) override {
    // Applying Groups to AnyCharacter would break the .∗ ≈ ¬∅ rewrite.
    if (exp == AnyCharacter()) {
      return exp;
    }
    // Applying Groups to the subexpressions will identify the leftmost.
    list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = Walk(sub);
      sub = Group(-1, sub, kPassive, false);
      subs.push_back(sub);
    }
    return Disjunction(subs, false);
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(ApplyGroups);
  ApplyGroups(const ApplyGroups&) = delete;
  void operator=(const ApplyGroups&) = delete;
};

class NumberGroups : public Walker {
 public:
  NumberGroups(vector<Mode>* modes, vector<int>* captures)
      : num_(0), modes_(modes), captures_(captures) {}
  ~NumberGroups() override {}

  Exp WalkGroup(Exp exp) override {
    Exp sub; Mode mode; bool capture;
    std::tie(std::ignore, sub, mode, capture) = exp->group();
    int num = num_++;
    modes_->push_back(mode);
    if (capture) {
      captures_->push_back(num);
    }
    sub = Walk(sub);
    return Group(num, sub, mode, capture);
  }

 private:
  int num_;
  vector<Mode>* modes_;
  vector<int>* captures_;

  //DISALLOW_COPY_AND_ASSIGN(NumberGroups);
  NumberGroups(const NumberGroups&) = delete;
  void operator=(const NumberGroups&) = delete;
};

class ExpandCharacterClasses : public Walker {
 public:
  ExpandCharacterClasses() {}
  ~ExpandCharacterClasses() override {}

  Exp WalkCharacterClass(Exp exp) override {
    list<Exp> subs;
    for (Rune character : exp->character_class().first) {
      subs.push_back(Character(character));
    }
    Exp tmp = Disjunction(subs, false);
    if (exp->character_class().second) {
      tmp = Conjunction(Complement(tmp), AnyCharacter());
    }
    return tmp;
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(ExpandCharacterClasses);
  ExpandCharacterClasses(const ExpandCharacterClasses&) = delete;
  void operator=(const ExpandCharacterClasses&) = delete;
};

class ExpandQuantifiers : public Walker {
 public:
  ExpandQuantifiers() {}
  ~ExpandQuantifiers() override {}

  Exp WalkQuantifier(Exp exp) override {
    Exp sub; int min; int max;
    std::tie(sub, min, max) = exp->quantifier();
    sub = Walk(sub);
    Exp tmp;
    if (max == -1) {
      tmp = KleeneClosure(sub);
    }
    while (max > min) {
      tmp = tmp == nullptr ? sub : Concatenation(sub, tmp);
      tmp = Disjunction(EmptyString(), tmp);
      --max;
    }
    while (min > 0) {
      tmp = tmp == nullptr ? sub : Concatenation(sub, tmp);
      --min;
    }
    tmp = tmp == nullptr ? EmptyString() : tmp;
    return tmp;
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(ExpandQuantifiers);
  ExpandQuantifiers(const ExpandQuantifiers&) = delete;
  void operator=(const ExpandQuantifiers&) = delete;
};

bool Parse(llvm::StringRef str, Exp* exp) {
  redgrep_yy::Data yydata(str, exp);
  redgrep_yy::parser parser(&yydata);
  if (parser.parse() == 0) {
    *exp = FlattenConjunctionsAndDisjunctions().Walk(*exp);
    *exp = StripGroups().Walk(*exp);
    *exp = ExpandCharacterClasses().Walk(*exp);
    *exp = ExpandQuantifiers().Walk(*exp);
    return true;
  }
  return false;
}

bool Parse(llvm::StringRef str, Exp* exp,
           vector<Mode>* modes, vector<int>* captures) {
  redgrep_yy::Data yydata(str, exp);
  redgrep_yy::parser parser(&yydata);
  if (parser.parse() == 0) {
    *exp = FlattenConjunctionsAndDisjunctions().Walk(*exp);
    *exp = ApplyGroups().Walk(*exp);
    *exp = NumberGroups(modes, captures).Walk(*exp);
    *exp = ExpandCharacterClasses().Walk(*exp);
    *exp = ExpandQuantifiers().Walk(*exp);
    return true;
  }
  return false;
}

bool Match(Exp exp, llvm::StringRef str) {
  while (!str.empty()) {
    int byte = str[0];
    str = str.drop_front(1);
    Exp der = Derivative(exp, byte);
    der = Normalised(der);
    exp = der;
  }
  bool match = IsNullable(exp);
  return match;
}

// Outputs the FA compiled from exp.
// If tagged is true, uses Antimirov partial derivatives to construct a TNFA.
// Otherwise, uses Brzozowski derivatives to construct a DFA.
inline size_t CompileImpl(Exp exp, bool tagged, FA* fa) {
  map<Exp, int> states;
  list<Exp> queue;
  auto LookupOrInsert = [&states, &queue](Exp exp) -> int {
    auto state = states.insert(make_pair(exp, states.size()));
    if (state.first->second > 0 &&
        state.second) {
      queue.push_back(exp);
    }
    return state.first->second;
  };
  queue.push_back(exp);
  while (!queue.empty()) {
    exp = queue.front();
    queue.pop_front();
    exp = Normalised(exp);
    int curr = LookupOrInsert(exp);
    if (exp->kind() == kEmptySet) {
      fa->error_ = curr;
    }
    if (exp->kind() == kEmptyString) {
      fa->empty_ = curr;
    }
    if (IsNullable(exp)) {
      fa->accepting_[curr] = true;
      if (tagged) {
        TNFA* tnfa = reinterpret_cast<TNFA*>(fa);
        EpsilonBindings(exp, &tnfa->final_[curr]);
      }
    } else {
      fa->accepting_[curr] = false;
    }
    list<bitset<256>>* partitions = &fa->partitions_[curr];
    Partitions(exp, partitions);
    for (list<bitset<256>>::const_iterator i = partitions->begin();
         i != partitions->end();
         ++i) {
      int byte;
      if (i == partitions->begin()) {
        // *i is Σ-based. Use a byte that it doesn't contain.
        byte = -1;
      } else {
        // *i is ∅-based. Use the first byte that it contains.
        for (byte = 0; !i->test(byte); ++byte) {}
      }
      if (tagged) {
        TNFA* tnfa = reinterpret_cast<TNFA*>(fa);
        Outer outer = Partial(exp, byte);
        set<pair<int, Bindings>> seen;
        for (const auto& j : *outer) {
          Exp par = Normalised(j.first);
          int next = LookupOrInsert(par);
          if (seen.count(make_pair(next, j.second)) == 0) {
            seen.insert(make_pair(next, j.second));
            if (i == partitions->begin()) {
              // Set the "default" transition.
              tnfa->transition_.insert(make_pair(
                  make_pair(curr, byte), make_pair(next, j.second)));
            } else {
              for (int byte = 0; byte < 256; ++byte) {
                if (i->test(byte)) {
                  tnfa->transition_.insert(make_pair(
                      make_pair(curr, byte), make_pair(next, j.second)));
                }
              }
            }
          }
        }
      } else {
        DFA* dfa = reinterpret_cast<DFA*>(fa);
        Exp der = Derivative(exp, byte);
        der = Normalised(der);
        int next = LookupOrInsert(der);
        if (i == partitions->begin()) {
          // Set the "default" transition.
          dfa->transition_[make_pair(curr, byte)] = next;
        } else {
          for (int byte = 0; byte < 256; ++byte) {
            if (i->test(byte)) {
              dfa->transition_[make_pair(curr, byte)] = next;
            }
          }
        }
      }
    }
  }
  return states.size();
}

size_t Compile(Exp exp, DFA* dfa) {
  return CompileImpl(exp, false, dfa);
}

size_t Compile(Exp exp, TNFA* tnfa) {
  return CompileImpl(exp, true, tnfa);
}

bool Match(const DFA& dfa, llvm::StringRef str) {
  int curr = 0;
  while (!str.empty()) {
    int byte = str[0];
    str = str.drop_front(1);
    auto transition = dfa.transition_.find(make_pair(curr, byte));
    if (transition == dfa.transition_.end()) {
      // Get the "default" transition.
      transition = dfa.transition_.find(make_pair(curr, -1));
    }
    int next = transition->second;
    curr = next;
  }
  return dfa.IsAccepting(curr);
}

// Applies the Bindings to offsets using pos.
static void ApplyBindings(const Bindings& bindings,
                          int pos,
                          vector<int>* offsets) {
  for (const auto& i : bindings) {
    int l = 2 * i.first + 0;
    int r = 2 * i.first + 1;
    switch (i.second) {
      case kCancel:
        if ((*offsets)[l] != -1) {
          (*offsets)[l] = -1;
          (*offsets)[r] = -1;
        }
        continue;
      case kEpsilon:
      case kAppend:
        if ((*offsets)[l] == -1) {
          (*offsets)[l] = pos;
          (*offsets)[r] = pos;
        }
        if (i.second == kAppend) {
          ++(*offsets)[r];
        }
        continue;
    }
    abort();
  }
}

// Returns true iff x precedes y in the total order specified by modes.
static bool Precedes(const vector<int>& x,
                     const vector<int>& y,
                     const vector<Mode>& modes) {
  for (size_t i = 0; i < modes.size(); ++i) {
    int l = 2 * i + 0;
    int r = 2 * i + 1;
    if (x[l] == -1 && y[l] == -1) {
      continue;
    } else if (x[l] == -1) {
      return false;
    } else if (y[l] == -1) {
      return true;
    } else if (modes[i] == kPassive) {
      continue;
    } else if (x[l] < y[l]) {
      return true;
    } else if (x[l] > y[l]) {
      return false;
    } else if (x[r] < y[r]) {
      return modes[i] == kMinimal;
    } else if (x[r] > y[r]) {
      return modes[i] == kMaximal;
    } else {
      continue;
    }
  }
  return false;
}

bool Match(const TNFA& tnfa, llvm::StringRef str,
           vector<int>* offsets) {
  auto CompareOffsets = [&tnfa](const pair<int, vector<int>>& x,
                                const pair<int, vector<int>>& y) -> bool {
    return Precedes(x.second, y.second, tnfa.modes_);
  };
  list<pair<int, vector<int>>> curr_states;
  curr_states.push_back(make_pair(0, vector<int>(2 * tnfa.modes_.size(), -1)));
  int pos = 0;
  while (!str.empty()) {
    int byte = str[0];
    str = str.drop_front(1);
    // For each current state, determine the next states - applying Bindings -
    // and then sort them by comparing offsets. Doing this repeatedly from the
    // initial state and discarding next states that have been seen already in
    // the current round is intended to simulate a VM implementation.
    list<pair<int, vector<int>>> next_states;
    set<int> seen;
    for (const auto& i : curr_states) {
      int curr = i.first;
      pair<int, int> key = make_pair(curr, byte);
      auto transition = tnfa.transition_.lower_bound(key);
      if (transition == tnfa.transition_.upper_bound(key)) {
        // Get the "default" transition.
        key = make_pair(curr, -1);
        transition = tnfa.transition_.lower_bound(key);
      }
      list<pair<int, vector<int>>> tmp;
      while (transition != tnfa.transition_.upper_bound(key)) {
        int next = transition->second.first;
        if (seen.count(next) == 0 &&
            !tnfa.IsError(next)) {
          seen.insert(next);
          vector<int> copy = i.second;
          ApplyBindings(transition->second.second, pos, &copy);
          tmp.push_back(make_pair(next, copy));
        }
        ++transition;
      }
      tmp.sort(CompareOffsets);
      next_states.insert(next_states.end(), tmp.begin(), tmp.end());
    }
    curr_states.swap(next_states);
    ++pos;
  }
  for (const auto& i : curr_states) {
    int curr = i.first;
    if (tnfa.IsAccepting(curr)) {
      vector<int> copy = i.second;
      ApplyBindings(tnfa.final_.find(curr)->second, pos, &copy);
      offsets->resize(2 * tnfa.captures_.size());
      for (size_t j = 0; j < tnfa.captures_.size(); ++j) {
        (*offsets)[2 * j + 0] = copy[2 * tnfa.captures_[j] + 0];
        (*offsets)[2 * j + 1] = copy[2 * tnfa.captures_[j] + 1];
      }
      return true;
    }
  }
  return false;
}

}  // namespace redgrep

namespace llvm {

template <>
class TypeBuilder<bool, false> : public TypeBuilder<types::i<1>, false> {};

}  // namespace llvm

namespace redgrep {

typedef bool NativeMatch(const char*, int);

Fun::Fun() {
  static std::once_flag once_flag;
  std::call_once(once_flag, []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  });
  context_.reset(new llvm::LLVMContext);
  module_ = new llvm::Module("M", *context_);
  engine_.reset(
      llvm::EngineBuilder(std::unique_ptr<llvm::Module>(module_)).create());
  function_ = llvm::Function::Create(
      llvm::TypeBuilder<NativeMatch, false>::get(*context_),
      llvm::GlobalValue::ExternalLinkage, "F", module_);
}

Fun::~Fun() {}

// Generates the function for the DFA.
static void GenerateFunction(const DFA& dfa, Fun* fun) {
  llvm::LLVMContext& context = *fun->context_;  // for convenience
  llvm::IRBuilder<> bb(context);

  // Create the entry BasicBlock and two automatic variables, then store the
  // Function Arguments in the automatic variables.
  llvm::BasicBlock* entry =
      llvm::BasicBlock::Create(context, "entry", fun->function_);
  bb.SetInsertPoint(entry);
  llvm::AllocaInst* data = bb.CreateAlloca(
      llvm::TypeBuilder<const char*, false>::get(context), 0, "data");
  llvm::AllocaInst* size = bb.CreateAlloca(
      llvm::TypeBuilder<int, false>::get(context), 0, "size");
  llvm::Function::arg_iterator arg = fun->function_->arg_begin();
  bb.CreateStore(arg++, data);
  bb.CreateStore(arg++, size);

  // Create a BasicBlock that returns true.
  llvm::BasicBlock* return_true =
      llvm::BasicBlock::Create(context, "return_true", fun->function_);
  bb.SetInsertPoint(return_true);
  bb.CreateRet(bb.getTrue());

  // Create a BasicBlock that returns false.
  llvm::BasicBlock* return_false =
      llvm::BasicBlock::Create(context, "return_false", fun->function_);
  bb.SetInsertPoint(return_false);
  bb.CreateRet(bb.getFalse());

  // Create two BasicBlocks per DFA state: the first branches if we have hit
  // the end of the string; the second switches to the next DFA state after
  // updating the automatic variables.
  vector<pair<llvm::BasicBlock*, llvm::BasicBlock*>> states;
  states.reserve(dfa.accepting_.size());
  for (const auto& i : dfa.accepting_) {
    llvm::BasicBlock* bb0 =
        llvm::BasicBlock::Create(context, "", fun->function_);
    llvm::BasicBlock* bb1 =
        llvm::BasicBlock::Create(context, "", fun->function_);

    bb.SetInsertPoint(bb0);
    bb.CreateCondBr(
        bb.CreateIsNull(bb.CreateLoad(size)),
        i.second ? return_true : return_false,
        bb1);

    bb.SetInsertPoint(bb1);
    llvm::LoadInst* byte = bb.CreateLoad(bb.CreateLoad(data));
    bb.CreateStore(bb.CreateGEP(bb.CreateLoad(data), bb.getInt32(1)), data);
    bb.CreateStore(bb.CreateSub(bb.CreateLoad(size), bb.getInt32(1)), size);
    // Set the "default" transition to ourselves for now. We could look it up,
    // but its BasicBlock might not exist yet, so we will just fix it up later.
    bb.CreateSwitch(byte, bb0);

    states.push_back(make_pair(bb0, bb1));
  }

  // Wire up the BasicBlocks.
  for (const auto& i : dfa.transition_) {
    // Get the current DFA state.
    llvm::BasicBlock* bb1 = states[i.first.first].second;
    llvm::SwitchInst* swi = llvm::cast<llvm::SwitchInst>(bb1->getTerminator());
    // Get the next DFA state.
    llvm::BasicBlock* bb0 = states[i.second].first;
    if (i.first.second == -1) {
      // Set the "default" transition.
      swi->setDefaultDest(bb0);
    } else {
      swi->addCase(
          llvm::ConstantInt::get(
              llvm::TypeBuilder<char, false>::get(context),
              i.first.second),
          bb0);
    }
  }

  // Plug in the entry BasicBlock.
  bb.SetInsertPoint(entry);
  bb.CreateBr(states[0].first);

  // Do we begin by scanning memory for a byte? If so, we can make memchr(3) do
  // that for us. It will almost certainly be vectorised and thus much faster.
  {
    llvm::BasicBlock* bb0 = states[0].first;
    llvm::BasicBlock* bb1 = states[0].second;
    llvm::BranchInst* bra = llvm::cast<llvm::BranchInst>(bb0->getTerminator());
    llvm::SwitchInst* swi = llvm::cast<llvm::SwitchInst>(bb1->getTerminator());
    if (swi->getDefaultDest() == bb0 &&
        swi->getNumCases() == 1) {
      // What is the byte that we are trying to find?
      fun->memchr_byte_ = swi->case_begin().getCaseValue()->getZExtValue();
      // What should we return if we fail to find it?
      fun->memchr_fail_ = bra->getSuccessor(0) == return_true;
    } else {
      fun->memchr_byte_ = -1;
    }
  }

  // Use the default optimisations.
  llvm::PassManagerBuilder opt;

  // Optimise the function.
  llvm::legacy::FunctionPassManager fpm(fun->module_);
  opt.populateFunctionPassManager(fpm);
  fpm.run(*fun->function_);

  // Optimise the module.
  llvm::legacy::PassManager mpm;
  opt.populateModulePassManager(mpm);
  mpm.run(*fun->module_);
}

// This seems to be the only way to discover the machine code size.
class DiscoverMachineCodeSize : public llvm::JITEventListener {
 public:
  explicit DiscoverMachineCodeSize(Fun* fun) : fun_(fun) {}
  ~DiscoverMachineCodeSize() override {}

  void NotifyObjectEmitted(
      const llvm::object::ObjectFile& object,
      const llvm::RuntimeDyld::LoadedObjectInfo& info) override {
    llvm::object::OwningBinary<llvm::object::ObjectFile> debug =
        info.getObjectForDebug(object);
    for (const llvm::object::SymbolRef& symbol : debug.getBinary()->symbols()) {
      llvm::StringRef name;
      symbol.getName(name);
      if (name == "F") {
        symbol.getAddress(fun_->machine_code_addr_);
        symbol.getSize(fun_->machine_code_size_);
        return;
      }
    }
    abort();
  }

 private:
  Fun* fun_;

  //DISALLOW_COPY_AND_ASSIGN(DiscoverMachineCodeSize);
  DiscoverMachineCodeSize(const DiscoverMachineCodeSize&) = delete;
  void operator=(const DiscoverMachineCodeSize&) = delete;
};

// Generates the machine code for the function.
static void GenerateMachineCode(Fun* fun) {
  DiscoverMachineCodeSize dmcs(fun);
  fun->engine_->RegisterJITEventListener(&dmcs);
  fun->engine_->finalizeObject();
  fun->engine_->UnregisterJITEventListener(&dmcs);
}

size_t Compile(const DFA& dfa, Fun* fun) {
  GenerateFunction(dfa, fun);
  GenerateMachineCode(fun);
  return fun->machine_code_size_;
}

bool Match(const Fun& fun, llvm::StringRef str) {
  if (fun.memchr_byte_ != -1) {
    const void* ptr = memchr(str.data(), fun.memchr_byte_, str.size());
    if (ptr == nullptr) {
      return fun.memchr_fail_;
    }
    str = str.drop_front(reinterpret_cast<const char*>(ptr) - str.data());
  }
  NativeMatch* match = reinterpret_cast<NativeMatch*>(fun.machine_code_addr_);
  return (*match)(str.data(), str.size());
}

}  // namespace redgrep
