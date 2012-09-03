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
#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Function.h"
#include "llvm/GlobalValue.h"
#include "llvm/IRBuilder.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/MutexGuard.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/TypeBuilder.h"
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

Expression::Expression(Kind kind, int byte)
    : kind_(kind),
      data_(byte),
      norm_(true) {}

Expression::Expression(Kind kind, const pair<int, int>& byte_range)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new pair<int, int>(byte_range))),
      norm_(true) {}

Expression::Expression(Kind kind, const list<Exp>& subexpressions, bool norm)
    : kind_(kind),
      data_(reinterpret_cast<intptr_t>(new list<Exp>(subexpressions))),
      norm_(norm) {}

Expression::~Expression() {
  switch (kind()) {
    case kEmptySet:
    case kEmptyString:
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

// TODO(junyer): EmptySet, EmptyString, AnyByte and AnyCharacter expressions
// could be singletons.

Exp EmptySet() {
  Exp exp(new Expression(kEmptySet));
  return exp;
}

Exp EmptyString() {
  Exp exp(new Expression(kEmptyString));
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
  Exp b1 = ByteRange(make_pair(0x00, 0x7F));  // 0xxxxxxx
  Exp bx = ByteRange(make_pair(0x80, 0xBF));  // 10xxxxxx
  Exp b2 = ByteRange(make_pair(0xC0, 0xDF));  // 110xxxxx
  Exp b3 = ByteRange(make_pair(0xE0, 0xEF));  // 1110xxxx
  Exp b4 = ByteRange(make_pair(0xF0, 0xF7));  // 11110xxx
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

    case kAnyByte:
      // ν(.) = ∅
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

Exp Derivative(Exp exp, int byte) {
  switch (exp->kind()) {
    case kEmptySet:
      // ∂a∅ = ∅
      return EmptySet();

    case kEmptyString:
      // ∂aε = ∅
      return EmptySet();

    case kAnyByte:
      // ∂a. = ε
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
      // Non-lazy form:
      // return Disjunction(Concatenation(Derivative(exp->head(), byte),
      //                                  exp->tail()),
      //                    Concatenation(Nullability(exp->head()),
      //                                  Derivative(exp->tail(), byte)));
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
      // C(ε) = {Σ}
      partitions->push_back({});
      return;

    case kAnyByte:
      // C(.) = {Σ}
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
          list<bitset<256>> x, y;
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
  while (!str.empty()) {
    int byte = str[0];
    str = str.substr(1);
    Exp der = Derivative(exp, byte);
    der = Normalised(der);
    exp = der;
  }
  bool match = IsNullable(exp);
  return match;
}

size_t Compile(Exp exp, DFA* dfa) {
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
    list<bitset<256>> partitions;
    Partitions(exp, &partitions);
    int next0;
    for (list<bitset<256>>::const_iterator i = partitions.begin();
         i != partitions.end();
         ++i) {
      int byte;
      if (i == partitions.begin()) {
        // *i is Σ-based. Use a byte that it doesn't contain. ;)
        byte = -1;
      } else {
        // *i is ∅-based. Use the first byte that it contains.
        for (byte = 0; !i->test(byte); ++byte) {}
      }
      Exp der = Derivative(exp, byte);
      der = Normalised(der);
      auto state = states.insert(make_pair(der, states.size()));
      int next = state.first->second;
      if (state.second) {
        queue.push_back(der);
      }
      if (i == partitions.begin()) {
        // Set the "default" transition.
        dfa->transition_[make_pair(curr, byte)] = next;
        next0 = next;
      } else if (next != next0) {
        for (byte = 0; byte < 256; ++byte) {
          if (i->test(byte)) {
            dfa->transition_[make_pair(curr, byte)] = next;
          }
        }
      }
    }
  }
  return states.size();
}

bool Match(const DFA& dfa, llvm::StringRef str) {
  int curr = 0;
  while (!str.empty()) {
    int byte = str[0];
    str = str.substr(1);
    auto transition = dfa.transition_.find(make_pair(curr, byte));
    if (transition == dfa.transition_.end()) {
      // Get the "default" transition.
      byte = -1;
      transition = dfa.transition_.find(make_pair(curr, byte));
    }
    int next = transition->second;
    curr = next;
  }
  auto accepting = dfa.accepting_.find(curr);
  bool match = accepting->second;
  return match;
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

static LLVM* llvm_singleton;
static pthread_once_t llvm_once = PTHREAD_ONCE_INIT;

static void LLVMOnce() {
  llvm::InitializeNativeTarget();

  LLVM* llvm = new LLVM;
  llvm->context_ = new llvm::LLVMContext;
  llvm->module_ = new llvm::Module("M", *llvm->context_);
  llvm->engine_ = llvm::EngineBuilder(llvm->module_).create();
  llvm->passes_ = new llvm::FunctionPassManager(llvm->module_);
  llvm->passes_->add(new llvm::TargetData(*llvm->engine_->getTargetData()));
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
        dfa.accepting_.find(curr)->second ? return_true : return_false,
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
      : llvm::JITEventListener(), fun_(fun) {}
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
