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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <bitset>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/MutexGuard.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#include "parser.tab.hh"
#include "utf/utf.h"

namespace redgrep {

using std::bitset;
using std::list;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::vector;

Expression::Expression(Kind kind)
    : kind_(kind),
      data_(0),
      norm_(true) {}

Expression::Expression(Kind kind, const pair<int, int>& tag)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new pair<int, int>(tag))),
      norm_(true) {}

Expression::Expression(Kind kind, int byte)
    : kind_(kind),
      data_(byte),
      norm_(true) {}

#if 0
// This is identical to the Tag constructor.
Expression::Expression(Kind kind, const pair<int, int>& byte_range)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new pair<int, int>(byte_range))),
      norm_(true) {}
#endif

Expression::Expression(Kind kind, const list<Exp>& subexpressions, bool norm)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new list<Exp>(subexpressions))),
      norm_(norm) {}

Expression::~Expression() {
  switch (kind()) {
    case kEmptySet:
    case kEmptyString:
      break;

    case kTag:
      delete reinterpret_cast<pair<int, int>*>(data());
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
  }
}

const pair<int, int>& Expression::tag() const {
  return *reinterpret_cast<pair<int, int>*>(data());
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

    case kTag:
      if (x->tag() < y->tag()) {
        return -1;
      }
      if (x->tag() > y->tag()) {
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

Exp Tag(const pair<int, int>& tag) {
  Exp exp(new Expression(kTag, tag));
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

Exp CharacterClass(const set<Rune>& character_class) {
  list<Exp> subs;
  for (Rune character : character_class) {
    subs.push_back(Character(character));
  }
  return Disjunction(subs, false);
}

Exp Normalised(Exp exp) {
  if (exp->norm()) {
    return exp;
  }
  switch (exp->kind()) {
    case kEmptySet:
    case kEmptyString:
    case kTag:
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

    case kTag:
      // Although Tag expressions behave like EmptyString in other ways, they
      // are not nullable.
      return false;

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
  }
  abort();
}

Exp Derivative(Exp exp, int byte) {
  switch (exp->kind()) {
    case kEmptySet:
      // ∂a∅ = ∅
      return EmptySet();

    case kEmptyString:
    case kTag:
      // ∂aε = ∅
      return EmptySet();

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
  }
  abort();
}

// Denormalises exp to a Conjunction (i.e. inner set) if necessary.
static Exp InnerSet(Exp exp) {
  exp = Normalised(exp);
  if (exp->kind() == kConjunction) {
    return exp;
  } else {
    return Conjunction({exp}, false);
  }
}

// Denormalises exp to a Disjunction (i.e. outer set) if necessary.
static Exp OuterSet(Exp exp) {
  exp = Normalised(exp);
  if (exp->kind() == kDisjunction) {
    list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = InnerSet(sub);
      subs.push_back(sub);
    }
    return Disjunction(subs, false);
  } else {
    exp = InnerSet(exp);
    return Disjunction({exp}, false);
  }
}

// Helper for building Concatenation expressions with partial derivatives.
static Exp PartialConcatenation(Exp head, Exp tail) {
  Exp outer = OuterSet(head);
  list<Exp> inners;
  for (Exp inner : outer->subexpressions()) {
    inner = Concatenation(inner, tail);
    inners.push_back(inner);
  }
  return Disjunction(inners, false);
}

// Helper for building Conjunction expressions with partial derivatives.
static Exp PartialConjunction(const list<Exp>& subs) {
  Exp outer;
  for (Exp sub : subs) {
    if (outer == nullptr) {
      outer = OuterSet(sub);
    } else {
      Exp x = outer;
      Exp y = OuterSet(sub);
      list<Exp> inners;
      for (Exp xinner : x->subexpressions()) {
        for (Exp yinner : y->subexpressions()) {
          Exp inner = Conjunction(xinner, yinner);
          inners.push_back(inner);
        }
      }
      outer = Disjunction(inners, false);
    }
  }
  return outer;
}

// Helper for building Complement expressions with partial derivatives.
// junyer's OCD wishes for this to be moved above PartialConjunction().
static Exp PartialComplement(Exp sub) {
  Exp outer = OuterSet(sub);
  list<Exp> inners;
  for (Exp inner : outer->subexpressions()) {
    list<Exp> subs;
    for (Exp sub : inner->subexpressions()) {
      sub = Complement(sub);
      subs.push_back(sub);
    }
    inner = Disjunction(subs, false);
    inners.push_back(inner);
  }
  return PartialConjunction(inners);
}

Exp Partial(Exp exp, int byte) {
  switch (exp->kind()) {
    case kEmptySet:
      // ∂a∅ = ∅
      return EmptySet();

    case kEmptyString:
    case kTag:
      // ∂aε = ∅
      return EmptySet();

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
      return PartialConcatenation(Partial(exp->sub(), byte),
                                  exp);

    case kConcatenation:
      // ∂a(r · s) = ∂ar · s + ν(r) · ∂as
      if (IsNullable(exp->head())) {
        return Disjunction(PartialConcatenation(Partial(exp->head(), byte),
                                                exp->tail()),
                           Partial(exp->tail(), byte));
      } else {
        return PartialConcatenation(Partial(exp->head(), byte),
                                    exp->tail());
      }

    case kComplement:
      // ∂a(¬r) = ¬(∂ar)
      return PartialComplement(Partial(exp->sub(), byte));

    case kConjunction: {
      // ∂a(r & s) = ∂ar & ∂as
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Partial(sub, byte);
        subs.push_back(sub);
      }
      return PartialConjunction(subs);
    }

    case kDisjunction: {
      // ∂a(r + s) = ∂ar + ∂as
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Partial(sub, byte);
        subs.push_back(sub);
      }
      return Disjunction(subs, false);
    }
  }
  abort();
}

Exp Epsilon(Exp exp) {
  switch (exp->kind()) {
    case kEmptySet:
    case kEmptyString:
    case kTag:
    case kAnyByte:
    case kByte:
    case kByteRange:
      return exp;

    case kKleeneClosure:
      return Disjunction(EmptyString(),
                         PartialConcatenation(Epsilon(exp->sub()),
                                              exp));

    case kConcatenation:
      if (IsNullable(exp->head())) {
        return Disjunction(PartialConcatenation(Epsilon(exp->head()),
                                                exp->tail()),
                           Epsilon(exp->tail()));
      } else {
        return PartialConcatenation(Epsilon(exp->head()),
                                    exp->tail());
      }

    case kComplement:
      return exp;

    case kConjunction: {
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Epsilon(sub);
        subs.push_back(sub);
      }
      return PartialConjunction(subs);
    }

    case kDisjunction: {
      list<Exp> subs;
      for (Exp sub : exp->subexpressions()) {
        sub = Epsilon(sub);
        subs.push_back(sub);
      }
      return Disjunction(subs, false);
    }
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
  partitions->clear();
  switch (exp->kind()) {
    case kEmptySet:
      // C(∅) = {Σ}
      partitions->push_back({});
      return;

    case kEmptyString:
    case kTag:
      // C(ε) = {Σ}
      partitions->push_back({});
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
  }
  abort();
}

// A simple framework for implementing the post-parse rewrites.
class WalkerBase {
 public:
  WalkerBase() {}
  virtual ~WalkerBase() {}

  virtual Exp WalkTag(Exp exp) {
    return exp;
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

  Exp Walk(Exp exp) {
    switch (exp->kind()) {
      case kEmptySet:
      case kEmptyString:
        return exp;

      case kTag:
        return WalkTag(exp);

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
    }
    abort();
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(WalkerBase);
  WalkerBase(const WalkerBase&) = delete;
  void operator=(const WalkerBase&) = delete;
};

class FlattenConjunctionsAndDisjunctions : public WalkerBase {
 public:
  FlattenConjunctionsAndDisjunctions() {}
  virtual ~FlattenConjunctionsAndDisjunctions() {}

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

  virtual Exp WalkConjunction(Exp exp) {
    list<Exp> subs;
    FlattenImpl(exp, &subs);
    return Conjunction(subs, false);
  }

  virtual Exp WalkDisjunction(Exp exp) {
    list<Exp> subs;
    FlattenImpl(exp, &subs);
    return Disjunction(subs, false);
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(FlattenConjunctionsAndDisjunctions);
  FlattenConjunctionsAndDisjunctions(const FlattenConjunctionsAndDisjunctions&) = delete;
  void operator=(const FlattenConjunctionsAndDisjunctions&) = delete;
};

class StripTags : public WalkerBase {
 public:
  StripTags() {}
  virtual ~StripTags() {}

  virtual Exp WalkConcatenation(Exp exp) {
    Exp head = Walk(exp->head());
    Exp tail = Walk(exp->tail());
    if (head->kind() == kTag) {
      return tail;
    }
    if (tail->kind() == kTag) {
      return head;
    }
    return Concatenation(head, tail);
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(StripTags);
  StripTags(const StripTags&) = delete;
  void operator=(const StripTags&) = delete;
};

class ApplyTagsWithinDisjunctions : public WalkerBase {
 public:
  ApplyTagsWithinDisjunctions() {}
  virtual ~ApplyTagsWithinDisjunctions() {}

  virtual Exp WalkDisjunction(Exp exp) {
    // Applying Tags to AnyCharacter or CharacterClass is a waste of space
    // and time. In the former case, it breaks the .∗ ≈ ¬∅ rewrite. In the
    // latter case, the number of subexpressions could be extremely large.
    bool unneeded = true;
    for (Exp sub : exp->subexpressions()) {
      while (sub->kind() == kConcatenation &&
             (sub->head()->kind() == kByte ||
              sub->head()->kind() == kByteRange)) {
        sub = sub->tail();
      }
      unneeded &= (sub->kind() == kByte ||
                   sub->kind() == kByteRange);
    }
    if (unneeded) {
      return exp;
    }

    list<Exp> subs;
    for (Exp sub : exp->subexpressions()) {
      sub = Walk(sub);
      // This left parenthesis (non-capturing) is passive.
      Exp left = Tag(0, 0);
      // This right parenthesis is passive.
      Exp right = Tag(1, 0);
      sub = Concatenation(left, sub, right);
      subs.push_back(sub);
    }
    return Disjunction(subs, false);
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(ApplyTagsWithinDisjunctions);
  ApplyTagsWithinDisjunctions(const ApplyTagsWithinDisjunctions&) = delete;
  void operator=(const ApplyTagsWithinDisjunctions&) = delete;
};

class NumberTags : public WalkerBase {
 public:
  explicit NumberTags(redgrep_yy::Data* yydata)
      : yydata_(yydata) {}
  virtual ~NumberTags() {}

  virtual Exp WalkTag(Exp exp) {
    return yydata_->Number(exp);
  }

 private:
  redgrep_yy::Data* yydata_;

  //DISALLOW_COPY_AND_ASSIGN(NumberTags);
  NumberTags(const NumberTags&) = delete;
  void operator=(const NumberTags&) = delete;
};

class StripTagsWithinComplements : public WalkerBase {
 public:
  StripTagsWithinComplements() {}
  virtual ~StripTagsWithinComplements() {}

  virtual Exp WalkComplement(Exp exp) {
    Exp sub = StripTags().Walk(exp->sub());
    return Complement(sub);
  }

 private:
  //DISALLOW_COPY_AND_ASSIGN(StripTagsWithinComplements);
  StripTagsWithinComplements(const StripTagsWithinComplements&) = delete;
  void operator=(const StripTagsWithinComplements&) = delete;
};

bool Parse(llvm::StringRef str, Exp* exp) {
  redgrep_yy::Data yydata(str, exp);
  redgrep_yy::parser parser(&yydata);
  if (parser.parse() == 0) {
    *exp = FlattenConjunctionsAndDisjunctions().Walk(*exp);
    *exp = StripTags().Walk(*exp);
    return true;
  }
  return false;
}

bool Parse(llvm::StringRef str, Exp* exp,
           vector<int>* modes, vector<int>* groups) {
  redgrep_yy::Data yydata(str, exp, modes, groups);
  redgrep_yy::parser parser(&yydata);
  if (parser.parse() == 0) {
    *exp = FlattenConjunctionsAndDisjunctions().Walk(*exp);
    *exp = ApplyTagsWithinDisjunctions().Walk(*exp);
    *exp = NumberTags(&yydata).Walk(*exp);
    *exp = StripTagsWithinComplements().Walk(*exp);
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
// If tagged is true, extended logic is enabled to construct a TNFA.
// Otherwise, standard logic is used to construct a DFA.
inline size_t CompileImpl(Exp exp, bool tagged, FA* fa) {
  fa->Clear();
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
    fa->accepting_[curr] = IsNullable(exp);
    if (tagged) {
      if (exp->kind() == kDisjunction) {
        for (Exp sub : exp->subexpressions()) {
          int next = LookupOrInsert(sub);
          fa->epsilon_.insert(make_pair(curr, make_pair(next, set<int>())));
        }
        continue;
      }
      set<int> tags;
      Exp inn = InnerSet(exp);
      list<Exp> subs;
      for (Exp sub : inn->subexpressions()) {
        while (sub->kind() == kConcatenation &&
               sub->head()->kind() == kTag) {
          tags.insert(sub->head()->tag().first);
          sub = sub->tail();
        }
        if (sub->kind() == kTag) {
          tags.insert(sub->tag().first);
          sub = EmptyString();
        }
        subs.push_back(sub);
      }
      inn = Conjunction(subs, false);
      inn = Normalised(inn);
      if (inn != exp) {
        int next = LookupOrInsert(inn);
        fa->epsilon_.insert(make_pair(curr, make_pair(next, tags)));
        continue;
      }
      Exp eps = Epsilon(exp);
      eps = Normalised(eps);
      if (eps != exp) {
        bool removed = false;
        if (eps->kind() == kDisjunction) {
          list<Exp> copy = eps->subexpressions();
          copy.remove(exp);
          if (copy != eps->subexpressions()) {
            removed = true;
            eps = Disjunction(copy, false);
            eps = Normalised(eps);
          }
        }
        int next = LookupOrInsert(eps);
        fa->epsilon_.insert(make_pair(curr, make_pair(next, set<int>())));
        if (!removed) {
          continue;
        }
      }
    }
    list<bitset<256>> partitions;
    Partitions(exp, &partitions);
    int next0;
    for (list<bitset<256>>::const_iterator i = partitions.begin();
         i != partitions.end();
         ++i) {
      int byte;
      if (i == partitions.begin()) {
        // *i is Σ-based. Use a byte that it doesn't contain.
        byte = -1;
      } else {
        // *i is ∅-based. Use the first byte that it contains.
        for (byte = 0; !i->test(byte); ++byte) {}
      }
      Exp der = tagged ? Partial(exp, byte) : Derivative(exp, byte);
      der = Normalised(der);
      int next = LookupOrInsert(der);
      if (i == partitions.begin()) {
        // Set the "default" transition.
        fa->transition_[make_pair(curr, byte)] = next;
        next0 = next;
      } else if (next != next0) {
        for (byte = 0; byte < 256; ++byte) {
          if (i->test(byte)) {
            fa->transition_[make_pair(curr, byte)] = next;
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
  return dfa.IsAcceptingState(curr);
}

// Returns true iff x precedes y in the total order specified by modes.
static bool Precedes(const vector<int>& x,
                     const vector<int>& y,
                     const vector<int>& modes) {
  for (int i = 0; i < modes.size(); ++i) {
    if (x[i] == y[i] ||
        // For passive mode, we continue if both are -1 or both are not -1.
        // Note that we don't bother checking if (x[i] == -1 && y[i] == -1)
        // because the preceding check covers that case.
        (modes[i] == 0 &&
         x[i] != -1 && y[i] != -1)) {
      continue;
    }
    switch (modes[i]) {
      case -1:
        return x[i] < y[i];
      case 0:
        return x[i] != -1;
      case 1:
        return x[i] > y[i];
      default:
        break;
    }
    abort();
  }
  return false;
}

// Follows ε-transitions in order to augment states with its ε-closure.
// The value of each tag seen will be set to pos.
static void FollowEpsilons(const TNFA& tnfa, int pos,
                           map<int, vector<int>>* states) {
  list<int> queue;
  for (const auto& i : *states) {
    int curr = i.first;
    queue.push_back(curr);
  }
  while (!queue.empty()) {
    int curr = queue.front();
    queue.pop_front();
    auto epsilon = tnfa.epsilon_.lower_bound(curr);
    while (epsilon != tnfa.epsilon_.upper_bound(curr)) {
      int next = epsilon->second.first;
      const set<int>& tags = epsilon->second.second;
      vector<int> copy = states->at(curr);
      for (int tag : tags) {
        copy[tag] = pos;
      }
      auto state = states->insert(make_pair(next, copy));
      if (state.second) {
        queue.push_back(next);
      } else if (Precedes(copy, state.first->second, tnfa.modes_)) {
        state.first->second = copy;
        queue.push_back(next);
      }
      ++epsilon;
    }
  }
}

bool Match(const TNFA& tnfa, llvm::StringRef str, vector<int>* values) {
  map<int, vector<int>> states;
  states[0].assign(tnfa.modes_.size(), -1);
  int pos = 0;
  FollowEpsilons(tnfa, pos, &states);
  while (!str.empty()) {
    int byte = str[0];
    str = str.drop_front(1);
    map<int, vector<int>> tmp;
    tmp.swap(states);
    for (const auto& i : tmp) {
      int curr = i.first;
      if (tnfa.IsGlueState(curr)) {
        continue;
      }
      auto transition = tnfa.transition_.find(make_pair(curr, byte));
      if (transition == tnfa.transition_.end()) {
        // Get the "default" transition.
        transition = tnfa.transition_.find(make_pair(curr, -1));
      }
      int next = transition->second;
      if (tnfa.IsErrorState(next)) {
        continue;
      }
      auto state = states.insert(make_pair(next, i.second));
      if (!state.second) {
        // This should never happen. If state X and state Y both transition to
        // state Z on byte B, then they should not have been separate states.
        abort();
      }
    }
    ++pos;
    FollowEpsilons(tnfa, pos, &states);
  }
  for (const auto& i : states) {
    int curr = i.first;
    // Note that a TNFA should have exactly one accepting state.
    if (tnfa.IsAcceptingState(curr)) {
      values->resize(tnfa.groups_.size());
      for (int j = 0; j < tnfa.groups_.size(); ++j) {
        (*values)[j] = i.second[tnfa.groups_[j]];
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

struct LLVM {
  llvm::sys::Mutex mutex_;
  llvm::LLVMContext* context_;
  llvm::Module* module_;
  llvm::ExecutionEngine* engine_;
  llvm::FunctionPassManager* passes_;
  int i_;  // monotonic counter, used for names
};

static pthread_once_t llvm_once = PTHREAD_ONCE_INIT;
static LLVM* llvm_singleton;

static void LLVMOnce() {
  llvm::InitializeNativeTarget();

  LLVM* llvm = new LLVM;
  llvm->context_ = new llvm::LLVMContext;
  llvm->module_ = new llvm::Module("M", *llvm->context_);
  llvm->engine_ = llvm::EngineBuilder(llvm->module_).create();
  llvm->passes_ = new llvm::FunctionPassManager(llvm->module_);
  llvm->passes_->add(new llvm::DataLayout(*llvm->engine_->getDataLayout()));
  llvm->passes_->add(llvm::createCodeGenPreparePass());
  llvm->passes_->add(llvm::createPromoteMemoryToRegisterPass());
  llvm->passes_->add(llvm::createLoopDeletionPass());
  llvm->i_ = 0;

  llvm_singleton = llvm;
}

static LLVM* GetLLVMSingleton() {
  pthread_once(&llvm_once, &LLVMOnce);
  return llvm_singleton;
}

typedef bool NativeMatch(const char*, int);

// Generates the function for the DFA.
static void GenerateFunction(const DFA& dfa, Fun* fun) {
  LLVM* llvm = GetLLVMSingleton();
  llvm::LLVMContext& context = *llvm->context_;
  llvm::Module* module = llvm->module_;
  char buf[64];  // scratch space, used for names

  // Create the Function.
  snprintf(buf, sizeof buf, "F%d", llvm->i_++);
  llvm::Function* function = llvm::Function::Create(
      llvm::TypeBuilder<NativeMatch, false>::get(context),
      llvm::GlobalValue::ExternalLinkage, buf, module);

  // Create the entry BasicBlock and two automatic variables, then store the
  // Function Arguments in the automatic variables.
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(
      context, "entry", function);
  llvm::IRBuilder<> bb(entry);
  llvm::AllocaInst* data = bb.CreateAlloca(
      llvm::TypeBuilder<const char*, false>::get(context), 0, "data");
  llvm::AllocaInst* size = bb.CreateAlloca(
      llvm::TypeBuilder<int, false>::get(context), 0, "size");
  llvm::Function::arg_iterator arg = function->arg_begin();
  bb.CreateStore(arg++, data);
  bb.CreateStore(arg++, size);

  // Create a BasicBlock that returns true.
  llvm::BasicBlock* return_true = llvm::BasicBlock::Create(
      context, "return_true", function);
  bb.SetInsertPoint(return_true);
  bb.CreateRet(bb.getTrue());

  // Create a BasicBlock that returns false.
  llvm::BasicBlock* return_false = llvm::BasicBlock::Create(
      context, "return_false", function);
  bb.SetInsertPoint(return_false);
  bb.CreateRet(bb.getFalse());

  // Create two BasicBlocks per DFA state: the first branches if we have hit
  // the end of the string; the second switches to the next DFA state after
  // updating the automatic variables.
  int nstates = dfa.transition_.rbegin()->first.first + 1;
  vector<pair<llvm::BasicBlock*, llvm::BasicBlock*>> states;
  states.reserve(nstates);
  for (int curr = 0; curr < nstates; ++curr) {
    llvm::BasicBlock* bb0 = llvm::BasicBlock::Create(context, "", function);
    llvm::BasicBlock* bb1 = llvm::BasicBlock::Create(context, "", function);

    bb.SetInsertPoint(bb0);
    bb.CreateCondBr(
        bb.CreateIsNull(bb.CreateLoad(size)),
        dfa.IsAcceptingState(curr) ? return_true : return_false,
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

  // Run the transform passes.
  llvm->passes_->run(*function);
  fun->function_ = function;
}

// This seems to be the only way to discover the machine code size.
class DiscoverMachineCodeSize : public llvm::JITEventListener {
 public:
  explicit DiscoverMachineCodeSize(Fun* fun)
      : fun_(fun) {}
  virtual ~DiscoverMachineCodeSize() {}

  virtual void NotifyFunctionEmitted(const llvm::Function&,
                                     void* addr,
                                     size_t size,
                                     const EmittedFunctionDetails&) {
    fun_->machine_code_addr_ = addr;
    fun_->machine_code_size_ = size;
  }

 private:
  Fun* fun_;

  //DISALLOW_COPY_AND_ASSIGN(DiscoverMachineCodeSize);
  DiscoverMachineCodeSize(const DiscoverMachineCodeSize&) = delete;
  void operator=(const DiscoverMachineCodeSize&) = delete;
};

// Generates the machine code for the function.
static void GenerateMachineCode(Fun* fun) {
  LLVM* llvm = GetLLVMSingleton();
  DiscoverMachineCodeSize dmcs(fun);
  llvm->engine_->RegisterJITEventListener(&dmcs);
  llvm->engine_->getPointerToFunction(fun->function_);
  llvm->engine_->UnregisterJITEventListener(&dmcs);
}

size_t Compile(const DFA& dfa, Fun* fun) {
  LLVM* llvm = GetLLVMSingleton();
  llvm::MutexGuard guard(llvm->mutex_);
  GenerateFunction(dfa, fun);
  GenerateMachineCode(fun);
  return fun->machine_code_size_;
}

bool Match(const Fun& fun, llvm::StringRef str) {
  NativeMatch* match = reinterpret_cast<NativeMatch*>(fun.machine_code_addr_);
  return (*match)(str.data(), str.size());
}

void Delete(const Fun& fun) {
  LLVM* llvm = GetLLVMSingleton();
  llvm::MutexGuard guard(llvm->mutex_);
  llvm->engine_->freeMachineCodeForFunction(fun.function_);
  fun.function_->eraseFromParent();
}

}  // namespace redgrep
