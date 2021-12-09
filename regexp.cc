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
#include "llvm/IR/Module.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "parser.tab.hh"
#include "utf.h"

namespace redgrep {

#define CAST_TO_INTPTR_T(ptr) reinterpret_cast<intptr_t>(ptr)

Expression::Expression(Kind kind)
    : kind_(kind),
      data_(0),
      norm_(true) {}

Expression::Expression(Kind kind, const std::tuple<int, Exp, Mode, bool>& group)
    : kind_(kind),
      data_(CAST_TO_INTPTR_T((new std::tuple<int, Exp, Mode, bool>(group)))),
      norm_(false) {}

Expression::Expression(Kind kind, int byte)
    : kind_(kind),
      data_(byte),
      norm_(true) {}

Expression::Expression(Kind kind, const std::pair<int, int>& byte_range)
    : kind_(kind),
      data_(CAST_TO_INTPTR_T((new std::pair<int, int>(byte_range)))),
      norm_(true) {}

Expression::Expression(Kind kind, const std::list<Exp>& subexpressions, bool norm)
    : kind_(kind),
      data_(CAST_TO_INTPTR_T((new std::list<Exp>(subexpressions)))),
      norm_(norm) {}

Expression::Expression(Kind kind, const std::pair<std::set<Rune>, bool>& character_class)
    : kind_(kind),
      data_(CAST_TO_INTPTR_T((new std::pair<std::set<Rune>, bool>(character_class)))),
      norm_(false) {}

Expression::Expression(Kind kind, const std::tuple<Exp, int, int>& quantifier)
    : kind_(kind),
      data_(CAST_TO_INTPTR_T((new std::tuple<Exp, int, int>(quantifier)))),
      norm_(false) {}

Expression::~Expression() {
  switch (kind()) {
    case kEmptySet:
    case kEmptyString:
      break;

    case kGroup:
      delete reinterpret_cast<std::tuple<int, Exp, Mode, bool>*>(data());
      break;

    case kAnyByte:
      break;

    case kByte:
      break;

    case kByteRange:
      delete reinterpret_cast<std::pair<int, int>*>(data());
      break;

    case kKleeneClosure:
    case kConcatenation:
    case kComplement:
    case kConjunction:
    case kDisjunction:
      delete reinterpret_cast<std::list<Exp>*>(data());
      break;

    case kCharacterClass:
      delete reinterpret_cast<std::pair<std::set<Rune>, bool>*>(data());
      break;

    case kQuantifier:
      delete reinterpret_cast<std::tuple<Exp, int, int>*>(data());
      break;
  }
}

const std::tuple<int, Exp, Mode, bool>& Expression::group() const {
  return *reinterpret_cast<std::tuple<int, Exp, Mode, bool>*>(data());
}

int Expression::byte() const {
  return data();
}

const std::pair<int, int>& Expression::byte_range() const {
  return *reinterpret_cast<std::pair<int, int>*>(data());
}

const std::list<Exp>& Expression::subexpressions() const {
  return *reinterpret_cast<std::list<Exp>*>(data());
}

const std::pair<std::set<Rune>, bool>& Expression::character_class() const {
  return *reinterpret_cast<std::pair<std::set<Rune>, bool>*>(data());
}

const std::tuple<Exp, int, int>& Expression::quantifier() const {
  return *reinterpret_cast<std::tuple<Exp, int, int>*>(data());
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
      std::list<Exp>::const_iterator xi = x->subexpressions().begin();
      std::list<Exp>::const_iterator yi = y->subexpressions().begin();
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

Exp Group(const std::tuple<int, Exp, Mode, bool>& group) {
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

Exp ByteRange(const std::pair<int, int>& byte_range) {
  Exp exp(new Expression(kByteRange, byte_range));
  return exp;
}

Exp KleeneClosure(const std::list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kKleeneClosure, subexpressions, norm));
  return exp;
}

Exp Concatenation(const std::list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kConcatenation, subexpressions, norm));
  return exp;
}

Exp Complement(const std::list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kComplement, subexpressions, norm));
  return exp;
}

Exp Conjunction(const std::list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kConjunction, subexpressions, norm));
  return exp;
}

Exp Disjunction(const std::list<Exp>& subexpressions, bool norm) {
  Exp exp(new Expression(kDisjunction, subexpressions, norm));
  return exp;
}

Exp CharacterClass(const std::pair<std::set<Rune>, bool>& character_class) {
  Exp exp(new Expression(kCharacterClass, character_class));
  return exp;
}

Exp Quantifier(const std::tuple<Exp, int, int>& quantifier) {
  Exp exp(new Expression(kQuantifier, quantifier));
  return exp;
}

Exp AnyCharacter() {
  Exp b1 = ByteRange(0x00, 0x7F);  // 0xxxxxxx
  Exp bx = ByteRange(0x80, 0xBF);  // 10xxxxxx
  Exp b2 = ByteRange(0xC2, 0xDF);  // 110xxxxx
  Exp b3 = ByteRange(0xE0, 0xEF);  // 1110xxxx
  Exp b4 = ByteRange(0xF0, 0xF4);  // 11110xxx
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
      std::list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Normalised(sub);
        // ∅ & r ≈ ∅
        // r & ∅ ≈ ∅
        if (sub->kind() == kEmptySet) {
          return sub;
        }
        // (r & s) & t ≈ r & (s & t)
        if (sub->kind() == kConjunction) {
          std::list<Exp> copy = sub->subexpressions();
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
      std::list<Exp> subs;
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
          std::list<Exp> copy = sub->subexpressions();
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
      std::list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Derivative(sub, byte);
        subs.push_back(sub);
      }
      return Conjunction(subs, false);
    }

    case kDisjunction: {
      // ∂a(r + s) = ∂ar + ∂as
      std::list<Exp> subs;
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
    outer->push_back(std::make_pair(sub, Bindings({})));
  }
  return outer;
}

Outer PartialConcatenation(Outer x, Exp y, const Bindings& initial) {
  // We mutate x as an optimisation.
  for (auto& xi : *x) {
    std::list<Exp> subs;
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
      tmp->push_back(std::make_pair(sub, Bindings({})));
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
      outer->push_back(std::make_pair(sub, bindings));
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
      bindings->push_back(std::make_pair(num, kCancel));
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
      bindings->push_back(std::make_pair(num, kEpsilon));
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
        i.second.push_back(std::make_pair(num, kAppend));
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
static void Intersection(const std::list<std::bitset<256>>& x,
                         const std::list<std::bitset<256>>& y,
                         std::list<std::bitset<256>>* z) {
  for (std::list<std::bitset<256>>::const_iterator xi = x.begin();
       xi != x.end();
       ++xi) {
    for (std::list<std::bitset<256>>::const_iterator yi = y.begin();
         yi != y.end();
         ++yi) {
      std::bitset<256> bs;
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

void Partitions(Exp exp, std::list<std::bitset<256>>* partitions) {
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
      std::bitset<256> bs;
      bs.set(exp->byte());
      partitions->push_back(bs);
      partitions->push_back(bs);
      return;
    }

    case kByteRange: {
      // C(S) = {Σ \ S, S}
      std::bitset<256> bs;
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
        std::list<std::bitset<256>> x, y;
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
          std::list<std::bitset<256>> x, y;
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
          std::list<std::bitset<256>> x, y;
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
    std::list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = Walk(sub);
      subs.push_back(sub);
    }
    return Conjunction(subs, false);
  }

  virtual Exp WalkDisjunction(Exp exp) {
    std::list<Exp> subs;
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
  Walker(const Walker&) = delete;
  Walker& operator=(const Walker&) = delete;
};

class FlattenConjunctionsAndDisjunctions : public Walker {
 public:
  FlattenConjunctionsAndDisjunctions() {}
  ~FlattenConjunctionsAndDisjunctions() override {}

  inline void FlattenImpl(Exp exp, std::list<Exp>* subs) {
    Kind kind = exp->kind();
    // In most cases, exp is a left-skewed binary tree.
    while (exp->kind() == kind &&
           exp->subexpressions().size() == 2) {
      subs->push_front(exp->tail());
      exp = exp->head();
    }
    if (exp->kind() == kind) {
      std::list<Exp> copy = exp->subexpressions();
      subs->splice(subs->begin(), copy);
    } else {
      subs->push_front(exp);
    }
    std::list<Exp>::iterator i = subs->begin();
    while (i != subs->end()) {
      Exp sub = *i;
      sub = Walk(sub);
      if (sub->kind() == kind) {
        std::list<Exp> copy = sub->subexpressions();
        subs->splice(i, copy);
        i = subs->erase(i);
      } else {
        *i = sub;
        ++i;
      }
    }
  }

  Exp WalkConjunction(Exp exp) override {
    std::list<Exp> subs;
    FlattenImpl(exp, &subs);
    return Conjunction(subs, false);
  }

  Exp WalkDisjunction(Exp exp) override {
    std::list<Exp> subs;
    FlattenImpl(exp, &subs);
    return Disjunction(subs, false);
  }

 private:
  FlattenConjunctionsAndDisjunctions(const FlattenConjunctionsAndDisjunctions&) = delete;
  FlattenConjunctionsAndDisjunctions& operator=(const FlattenConjunctionsAndDisjunctions&) = delete;
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
  StripGroups(const StripGroups&) = delete;
  StripGroups& operator=(const StripGroups&) = delete;
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
    std::list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = Walk(sub);
      sub = Group(-1, sub, kPassive, false);
      subs.push_back(sub);
    }
    return Disjunction(subs, false);
  }

 private:
  ApplyGroups(const ApplyGroups&) = delete;
  ApplyGroups& operator=(const ApplyGroups&) = delete;
};

class NumberGroups : public Walker {
 public:
  NumberGroups(std::vector<Mode>* modes, std::vector<int>* captures)
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
  std::vector<Mode>* modes_;
  std::vector<int>* captures_;

  NumberGroups(const NumberGroups&) = delete;
  NumberGroups& operator=(const NumberGroups&) = delete;
};

class ExpandCharacterClasses : public Walker {
 public:
  ExpandCharacterClasses() {}
  ~ExpandCharacterClasses() override {}

  Exp WalkCharacterClass(Exp exp) override {
    std::list<Exp> subs;
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
  ExpandCharacterClasses(const ExpandCharacterClasses&) = delete;
  ExpandCharacterClasses& operator=(const ExpandCharacterClasses&) = delete;
};

class ExpandQuantifiers : public Walker {
 public:
  ExpandQuantifiers(bool* exceeded)
      : exceeded_(exceeded), stack_({1000}) {}
  ~ExpandQuantifiers() override {}

  Exp WalkQuantifier(Exp exp) override {
    Exp sub; int min; int max;
    std::tie(sub, min, max) = exp->quantifier();
    // Validate the repetition.
    int limit = stack_.back();
    int rep = max;
    if (rep == -1) {
      rep = min;
    }
    if (rep > 0) {
      limit /= rep;
    }
    if (limit == 0) {
      *exceeded_ = true;
      return exp;
    }
    stack_.push_back(limit);
    sub = Walk(sub);
    stack_.pop_back();
    if (*exceeded_) {
      return exp;
    }
    // Perform the repetition.
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
  bool* exceeded_;
  std::vector<int> stack_;

  ExpandQuantifiers(const ExpandQuantifiers&) = delete;
  ExpandQuantifiers& operator=(const ExpandQuantifiers&) = delete;
};

bool Parse(llvm::StringRef str, Exp* exp) {
  redgrep_yy::Data yydata(str, exp);
  redgrep_yy::parser parser(&yydata);
  if (parser.parse() == 0) {
    *exp = FlattenConjunctionsAndDisjunctions().Walk(*exp);
    *exp = StripGroups().Walk(*exp);
    *exp = ExpandCharacterClasses().Walk(*exp);
    bool exceeded = false;
    *exp = ExpandQuantifiers(&exceeded).Walk(*exp);
    return !exceeded;
  }
  return false;
}

bool Parse(llvm::StringRef str, Exp* exp,
           std::vector<Mode>* modes, std::vector<int>* captures) {
  redgrep_yy::Data yydata(str, exp);
  redgrep_yy::parser parser(&yydata);
  if (parser.parse() == 0) {
    *exp = FlattenConjunctionsAndDisjunctions().Walk(*exp);
    *exp = ApplyGroups().Walk(*exp);
    *exp = NumberGroups(modes, captures).Walk(*exp);
    *exp = ExpandCharacterClasses().Walk(*exp);
    bool exceeded = false;
    *exp = ExpandQuantifiers(&exceeded).Walk(*exp);
    return !exceeded;
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
  std::map<Exp, int> states;
  std::list<Exp> queue;
  auto LookupOrInsert = [&states, &queue](Exp exp) -> int {
    auto state = states.insert(std::make_pair(exp, states.size()));
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
    std::list<std::bitset<256>>* partitions = &fa->partitions_[curr];
    Partitions(exp, partitions);
    for (std::list<std::bitset<256>>::const_iterator i = partitions->begin();
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
        std::set<std::pair<int, Bindings>> seen;
        for (const auto& j : *outer) {
          Exp par = Normalised(j.first);
          int next = LookupOrInsert(par);
          if (seen.count(std::make_pair(next, j.second)) == 0) {
            seen.insert(std::make_pair(next, j.second));
            if (i == partitions->begin()) {
              // Set the "default" transition.
              tnfa->transition_.insert(std::make_pair(
                  std::make_pair(curr, byte), std::make_pair(next, j.second)));
            } else {
              for (int byte = 0; byte < 256; ++byte) {
                if (i->test(byte)) {
                  tnfa->transition_.insert(std::make_pair(
                      std::make_pair(curr, byte), std::make_pair(next, j.second)));
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
          dfa->transition_[std::make_pair(curr, byte)] = next;
        } else {
          for (int byte = 0; byte < 256; ++byte) {
            if (i->test(byte)) {
              dfa->transition_[std::make_pair(curr, byte)] = next;
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
    auto transition = dfa.transition_.find(std::make_pair(curr, byte));
    if (transition == dfa.transition_.end()) {
      // Get the "default" transition.
      transition = dfa.transition_.find(std::make_pair(curr, -1));
    }
    int next = transition->second;
    curr = next;
  }
  return dfa.IsAccepting(curr);
}

// Applies the Bindings to offsets using pos.
static void ApplyBindings(const Bindings& bindings,
                          int pos,
                          std::vector<int>* offsets) {
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
static bool Precedes(const std::vector<int>& x,
                     const std::vector<int>& y,
                     const std::vector<Mode>& modes) {
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
           std::vector<int>* offsets) {
  auto CompareOffsets = [&tnfa](const std::pair<int, std::vector<int>>& x,
                                const std::pair<int, std::vector<int>>& y) -> bool {
    return Precedes(x.second, y.second, tnfa.modes_);
  };
  std::list<std::pair<int, std::vector<int>>> curr_states;
  curr_states.push_back(std::make_pair(0, std::vector<int>(2 * tnfa.modes_.size(), -1)));
  int pos = 0;
  while (!str.empty()) {
    int byte = str[0];
    str = str.drop_front(1);
    // For each current state, determine the next states - applying Bindings -
    // and then sort them by comparing offsets. Doing this repeatedly from the
    // initial state and discarding next states that have been seen already in
    // the current round is intended to simulate a VM implementation.
    std::list<std::pair<int, std::vector<int>>> next_states;
    std::set<int> seen;
    for (const auto& i : curr_states) {
      int curr = i.first;
      std::pair<int, int> key = std::make_pair(curr, byte);
      auto transition = tnfa.transition_.lower_bound(key);
      if (transition == tnfa.transition_.upper_bound(key)) {
        // Get the "default" transition.
        key = std::make_pair(curr, -1);
        transition = tnfa.transition_.lower_bound(key);
      }
      std::list<std::pair<int, std::vector<int>>> tmp;
      while (transition != tnfa.transition_.upper_bound(key)) {
        int next = transition->second.first;
        if (seen.count(next) == 0 &&
            !tnfa.IsError(next)) {
          seen.insert(next);
          std::vector<int> copy = i.second;
          ApplyBindings(transition->second.second, pos, &copy);
          tmp.push_back(std::make_pair(next, copy));
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
      std::vector<int> copy = i.second;
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

typedef bool NativeMatch(const char*, size_t);

static llvm::FunctionType* getNativeMatchFnTy(llvm::LLVMContext& context) {
  return llvm::FunctionType::get(llvm::Type::getInt1Ty(context),
                                 {llvm::Type::getInt8PtrTy(context),
                                  llvm::Type::getScalarTy<size_t>(context)},
                                 false);
}

Fun::Fun() {
  static std::once_flag once_flag;
  std::call_once(once_flag, []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  });
  context_.reset(new llvm::LLVMContext);
  module_ = new llvm::Module("M", *context_);
  engine_.reset(llvm::EngineBuilder(std::unique_ptr<llvm::Module>(module_))
                    .setMCPU(llvm::sys::getHostCPUName())
                    .create());
  function_ =
      llvm::Function::Create(getNativeMatchFnTy(*context_),
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
      llvm::Type::getInt8PtrTy(context), nullptr, "data");
  llvm::AllocaInst* size = bb.CreateAlloca(
      llvm::Type::getScalarTy<size_t>(context), nullptr, "size");
  llvm::Function::arg_iterator arg = fun->function_->arg_begin();
  bb.CreateStore(&*arg++, data);
  bb.CreateStore(&*arg++, size);

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
  std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> states;
  states.reserve(dfa.accepting_.size());
  for (const auto& i : dfa.accepting_) {
    llvm::BasicBlock* bb0 =
        llvm::BasicBlock::Create(context, "", fun->function_);
    llvm::BasicBlock* bb1 =
        llvm::BasicBlock::Create(context, "", fun->function_);

    auto sizeTy = llvm::Type::getScalarTy<size_t>(context);
    auto int8PtrTy = llvm::Type::getInt8PtrTy(context);
    auto int8Ty = llvm::Type::getInt8Ty(context);

    bb.SetInsertPoint(bb0);
    bb.CreateCondBr(
        bb.CreateIsNull(bb.CreateLoad(sizeTy, size)),
        i.second ? return_true : return_false,
        bb1);

    bb.SetInsertPoint(bb1);
    llvm::LoadInst* bytep = bb.CreateLoad(int8PtrTy, data);
    llvm::LoadInst* byte = bb.CreateLoad(int8Ty, bytep);
    bb.CreateStore(
        bb.CreateGEP(int8Ty, bytep, bb.getInt64(1)),
        data);
    bb.CreateStore(
        bb.CreateSub(bb.CreateLoad(sizeTy, size), bb.getInt64(1)),
        size);
    // Set the "default" transition to ourselves for now. We could look it up,
    // but its BasicBlock might not exist yet, so we will just fix it up later.
    bb.CreateSwitch(byte, bb0);

    states.push_back(std::make_pair(bb0, bb1));
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
      swi->addCase(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context),
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
      fun->memchr_byte_ = swi->case_begin()->getCaseValue()->getZExtValue();
      // What should we return if we fail to find it?
      fun->memchr_fail_ = bra->getSuccessor(0) == return_true;
    } else {
      fun->memchr_byte_ = -1;
    }
  }

  // Optimise the module.
  // NOTE(junyer): This was cargo-culted from Clang. Ordering matters!
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cam;
  llvm::ModuleAnalysisManager mam;

  llvm::PassBuilder pb(fun->engine_->getTargetMachine());
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.registerModuleAnalyses(mam);
  pb.crossRegisterProxies(lam, fam, cam, mam);

  llvm::ModulePassManager mpm =
      pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  mpm.run(*fun->module_, mam);
}

// This seems to be the only way to discover the machine code size.
class DiscoverMachineCodeSize : public llvm::JITEventListener {
 public:
  explicit DiscoverMachineCodeSize(Fun* fun) : fun_(fun) {}
  ~DiscoverMachineCodeSize() override {}

  void
  notifyObjectLoaded(ObjectKey, const llvm::object::ObjectFile &object,
                     const llvm::RuntimeDyld::LoadedObjectInfo &info) override {
    // We need this in order to obtain the addresses as well as the sizes.
    llvm::object::OwningBinary<llvm::object::ObjectFile> debug =
        info.getObjectForDebug(object);
    std::vector<std::pair<llvm::object::SymbolRef, uint64_t>> symbol_sizes =
        llvm::object::computeSymbolSizes(*debug.getBinary());
    for (const auto& i : symbol_sizes) {
      llvm::Expected<llvm::StringRef> name = i.first.getName();
      llvm::Expected<uint64_t> addr = i.first.getAddress();
      if (name && addr && *name == "F") {
        fun_->machine_code_addr_ = *addr;
        fun_->machine_code_size_ = i.second;
        return;
      }
    }
    abort();
  }

 private:
  Fun* fun_;

  DiscoverMachineCodeSize(const DiscoverMachineCodeSize&) = delete;
  DiscoverMachineCodeSize& operator=(const DiscoverMachineCodeSize&) = delete;
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
