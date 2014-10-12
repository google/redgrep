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

#ifndef REDGREP_REGEXP_H_
#define REDGREP_REGEXP_H_

#include <stddef.h>
#include <stdint.h>

#include <bitset>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "utf/utf.h"

namespace llvm {
class ExecutionEngine;
class Function;
class LLVMContext;
class Module;
}  // namespace llvm

namespace redgrep {

using std::bitset;
using std::list;
using std::make_pair;
using std::make_tuple;
using std::map;
using std::multimap;
using std::pair;
using std::set;
using std::tuple;
using std::vector;

// Implements regular expressions using Brzozowski derivatives, Antimirov
// partial derivatives, Sulzmann submatches and Laurikari tagged transitions.
//
// References
// ----------
//
// "Derivatives of Regular Expressions"
// Janusz Brzozowski
// Journal of the ACM, vol. 11 iss. 4, pp. 481-494, October 1964
// http://dl.acm.org/citation.cfm?id=321249
//
// "Regular-expression derivatives re-examined"
// Scott Owens, John Reppy, Aaron Turon
// Journal of Functional Programming, vol. 19 iss. 2, pp. 173-190, March 2009
// http://dl.acm.org/citation.cfm?id=1520288
//
// "Partial Derivatives of Regular Expressions and Finite Automaton Constructions"
// Valentin Antimirov
// Theoretical Computer Science, vol. 155 iss. 2, pp. 291-319, March 1996
// http://dl.acm.org/citation.cfm?id=231848
//
// "Partial Derivatives of an Extended Regular Expression"
// Pascal Caron, Jean-Marc Champarnaud, Ludovic Mignot
// Language and Automata Theory and Applications 2011, pp. 179-191, May 2011
// http://dl.acm.org/citation.cfm?id=2022911
//
// "A Flexible and Efficient ML Lexer Tool Based on Extended Regular Expression Submatching"
// Martin Sulzmann, Pippijn van Steenhoven
// Compiler Construction 2014, pp. 174-191, April 2014
// http://dx.doi.org/10.1007/978-3-642-54807-9_10
//
// "Efficient submatch addressing for regular expressions"
// Ville Laurikari
// Master's Thesis, November 2001
// http://laurikari.net/ville/regex-submatch.pdf

enum Kind {
  kEmptySet,
  kEmptyString,
  kGroup,
  kAnyByte,
  kByte,
  kByteRange,
  kKleeneClosure,
  kConcatenation,
  kComplement,
  kConjunction,
  kDisjunction,
  kCharacterClass,  // ephemeral
  kQuantifier,      // ephemeral
};

enum Mode {
  kMinimal,
  kPassive,
  kMaximal,
};

class Expression;
typedef std::shared_ptr<Expression> Exp;

// Represents a regular expression.
// Note that the data members are const in order to guarantee immutability,
// which will matter later when we use expressions as STL container keys.
class Expression {
 public:
  explicit Expression(Kind kind);
  Expression(Kind kind, const tuple<int, Exp, Mode, bool>& group);
  Expression(Kind kind, int byte);
  Expression(Kind kind, const pair<int, int>& byte_range);
  Expression(Kind kind, const list<Exp>& subexpressions, bool norm);
  Expression(Kind kind, const pair<set<Rune>, bool>& character_class);
  Expression(Kind kind, const tuple<Exp, int, int>& quantifier);
  ~Expression();

  Kind kind() const { return kind_; }
  intptr_t data() const { return data_; }
  bool norm() const { return norm_; }

  // Accessors for the expression data. Of course, if you call the wrong
  // function for the expression kind, you're gonna have a bad time.
  const tuple<int, Exp, Mode, bool>& group() const;
  int byte() const;
  const pair<int, int>& byte_range() const;
  const list<Exp>& subexpressions() const;
  const pair<set<Rune>, bool>& character_class() const;
  const tuple<Exp, int, int>& quantifier() const;

  // A KleeneClosure or Complement expression has one subexpression.
  // Use sub() for convenience.
  Exp sub() const { return subexpressions().front(); }

  // A Concatenation expression has two subexpressions, the second typically
  // being another Concatenation. Thus, the concept of "head" and "tail".
  // Use head() and tail() for convenience.
  Exp head() const { return subexpressions().front(); }
  Exp tail() const { return subexpressions().back(); }

 private:
  const Kind kind_;
  const intptr_t data_;
  const bool norm_;

  //DISALLOW_COPY_AND_ASSIGN(Expression);
  Expression(const Expression&) = delete;
  void operator=(const Expression&) = delete;
};

// Returns -1, 0 or +1 when x is less than, equal to or greater than y,
// respectively, so that we can define operators et cetera for convenience.
int Compare(Exp x, Exp y);

}  // namespace redgrep

namespace std {

#define REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(op, fun) \
                                                            \
  inline bool operator op(redgrep::Exp x, redgrep::Exp y) { \
    return redgrep::Compare(x, y) op 0;                     \
  }                                                         \
                                                            \
  template <>                                               \
  struct fun<redgrep::Exp> {                                \
    bool operator()(redgrep::Exp x, redgrep::Exp y) const { \
      return x op y;                                        \
    }                                                       \
  };

REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(==, equal_to)
REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(!=, not_equal_to)
REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(<, less)
REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(<=, less_equal)
REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(>, greater)
REDGREP_COMPARE_DEFINE_OPERATORS_ET_CETERA(>=, greater_equal)

}  // namespace std

namespace redgrep {

// Builders for the various expression kinds.
// Use the inline functions for convenience when building up expressions in
// parser code, test code et cetera.

Exp EmptySet();
Exp EmptyString();
Exp Group(const tuple<int, Exp, Mode, bool>& group);
Exp AnyByte();
Exp Byte(int byte);
Exp ByteRange(const pair<int, int>& byte_range);
Exp KleeneClosure(const list<Exp>& subexpressions, bool norm);
Exp Concatenation(const list<Exp>& subexpressions, bool norm);
Exp Complement(const list<Exp>& subexpressions, bool norm);
Exp Conjunction(const list<Exp>& subexpressions, bool norm);
Exp Disjunction(const list<Exp>& subexpressions, bool norm);
Exp CharacterClass(const pair<set<Rune>, bool>& character_class);
Exp Quantifier(const tuple<Exp, int, int>& quantifier);

inline Exp Group(int num, Exp sub, Mode mode, bool capture) {
  return Group(make_tuple(num, sub, mode, capture));
}

inline Exp ByteRange(int min, int max) {
  return ByteRange(make_pair(min, max));
}

inline Exp KleeneClosure(Exp x) {
  return KleeneClosure({x}, false);
}

inline Exp Concatenation(Exp x, Exp y) {
  return Concatenation({x, y}, false);
}

template <typename... Variadic>
inline Exp Concatenation(Exp x, Exp y, Variadic... z) {
  return Concatenation({x, Concatenation(y, z...)}, false);
}

inline Exp Complement(Exp x) {
  return Complement({x}, false);
}

template <typename... Variadic>
inline Exp Conjunction(Exp x, Exp y, Variadic... z) {
  return Conjunction({x, y, z...}, false);
}

template <typename... Variadic>
inline Exp Disjunction(Exp x, Exp y, Variadic... z) {
  return Disjunction({x, y, z...}, false);
}

inline Exp CharacterClass(const set<Rune>& characters, bool complement) {
  return CharacterClass(make_pair(characters, complement));
}

inline Exp Quantifier(Exp sub, int min, int max) {
  return Quantifier(make_tuple(sub, min, max));
}

Exp AnyCharacter();
Exp Character(Rune character);

// Returns the normalised form of exp.
Exp Normalised(Exp exp);

// Returns the nullability of exp as a bool.
// EmptySet and EmptyString map to false and true, respectively.
bool IsNullable(Exp exp);

// Returns the derivative of exp with respect to byte.
Exp Derivative(Exp exp, int byte);

enum BindingType {
  kCancel,
  kEpsilon,
  kAppend,
};

typedef list<pair<int, BindingType>> Bindings;

// Conceptually, an OuterSet is a Disjunction and an InnerSet is a Conjunction.
// For simplicity, we don't introduce a new type for the latter, but the former
// needs to associate each InnerSet with its Bindings.
typedef list<pair<Exp, Bindings>> OuterSet;
typedef std::unique_ptr<OuterSet> Outer;

// Returns the denormalised form of exp.
Outer Denormalised(Exp exp);

// Partial() helpers for building OuterSets. Exposed for ease of testing.
Outer PartialConcatenation(Outer x, Exp y, const Bindings& initial);
Outer PartialComplement(Outer x);
Outer PartialConjunction(Outer x, Outer y);
Outer PartialDisjunction(Outer x, Outer y);

// Returns the partial derivative of exp with respect to byte.
Outer Partial(Exp exp, int byte);

// Outputs the partitions computed for exp.
// The first partition should be Σ-based. Any others should be ∅-based.
void Partitions(Exp exp, list<bitset<256>>* partitions);

// Outputs the expression parsed from str.
// Returns true on success, false on failure.
bool Parse(llvm::StringRef str, Exp* exp);

// Outputs the expression parsed from str as well as the mode of each Group and
// which Groups capture.
// Returns true on success, false on failure.
bool Parse(llvm::StringRef str, Exp* exp,
           vector<Mode>* modes, vector<int>* captures);

// Returns the result of matching str using exp.
bool Match(Exp exp, llvm::StringRef str);

// Represents a finite automaton.
class FA {
 public:
  FA() : error_(-1) {}
  virtual ~FA() {}

  bool IsError(int state) const {
    return state == error_;
  }

  bool IsAccepting(int state) const {
    return accepting_.find(state)->second;
  }

  int error_;
  map<int, bool> accepting_;
  map<int, list<bitset<256>>> partitions_;

 private:
  //DISALLOW_COPY_AND_ASSIGN(FA);
  FA(const FA&) = delete;
  void operator=(const FA&) = delete;
};

// Represents a deterministic finite automaton.
class DFA : public FA {
 public:
  DFA() {}
  ~DFA() override {}

  map<pair<int, int>, int> transition_;

 private:
  //DISALLOW_COPY_AND_ASSIGN(DFA);
  DFA(const DFA&) = delete;
  void operator=(const DFA&) = delete;
};

// Represents a tagged nondeterministic finite automaton.
class TNFA : public FA {
 public:
  TNFA() {}
  ~TNFA() override {}

  vector<Mode> modes_;
  vector<int> captures_;

  multimap<pair<int, int>, pair<int, Bindings>> transition_;
  map<int, Bindings> final_;

 private:
  //DISALLOW_COPY_AND_ASSIGN(TNFA);
  TNFA(const TNFA&) = delete;
  void operator=(const TNFA&) = delete;
};

// Outputs the DFA compiled from exp.
// Returns the number of DFA states.
size_t Compile(Exp exp, DFA* dfa);

// Outputs the TNFA compiled from exp.
// Returns the number of TNFA states.
size_t Compile(Exp exp, TNFA* tnfa);

// Returns the result of matching str using dfa.
bool Match(const DFA& dfa, llvm::StringRef str);

// Returns the result of matching str using tnfa.
// Outputs the offsets of the beginning and ending of each Group that captures.
// Thus, the nth Group begins at offsets[2*n+0] and ends at offsets[2*n+1].
bool Match(const TNFA& tnfa, llvm::StringRef str,
           vector<int>* offsets);

// Represents a function and its machine code.
struct Fun {
  Fun();
  ~Fun();

  std::unique_ptr<llvm::LLVMContext> context_;
  llvm::Module* module_;  // Not owned.
  std::unique_ptr<llvm::ExecutionEngine> engine_;
  llvm::Function* function_;  // Not owned.

  int memchr_byte_;
  bool memchr_fail_;

  uint64_t machine_code_addr_;
  uint64_t machine_code_size_;
};

// Outputs the function compiled from dfa.
// Returns the number of bytes of machine code.
size_t Compile(const DFA& dfa, Fun* fun);

// Returns the result of matching str using fun.
bool Match(const Fun& fun, llvm::StringRef str);

}  // namespace redgrep

#endif  // REDGREP_REGEXP_H_
