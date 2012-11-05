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
#include <utility>

#include "llvm/ADT/StringRef.h"
#include "utf/utf.h"

namespace llvm {
class Function;
}  // namespace llvm

namespace redgrep {

using std::bitset;
using std::list;
using std::make_pair;
using std::map;
using std::pair;
using std::set;

// Implements regular expressions using Brzozowski derivatives.
//
// References
// ----------
//
// "Derivatives of Regular Expressions"
// Janusz A. Brzozowski
// Journal of the ACM (JACM), vol. 11 iss. 4, pp. 481-494, October 1964
// http://dl.acm.org/citation.cfm?id=321249
//
// "Regular-expression derivatives re-examined"
// Scott Owens, John Reppy, Aaron Turon
// Journal of Functional Programming, vol. 19 iss. 2, pp. 173-190, March 2009
// http://dl.acm.org/citation.cfm?id=1520288

enum Kind {
  kEmptySet,
  kEmptyString,
  kAnyByte,
  kByte,
  kByteRange,
  kKleeneClosure,
  kConcatenation,
  kComplement,
  kConjunction,
  kDisjunction,
};

class Expression;
typedef std::shared_ptr<Expression> Exp;

// Represents a regular expression.
// Note that the data members are const in order to guarantee immutability,
// which will matter later when we use expressions as STL container keys.
class Expression {
 public:
  explicit Expression(Kind kind);
  Expression(Kind kind, int byte);
  Expression(Kind kind, const pair<int, int>& byte_range);
  Expression(Kind kind, const list<Exp>& subexpressions, bool norm);
  virtual ~Expression();

  Kind kind() const { return kind_; }
  intptr_t data() const { return data_; }
  bool norm() const { return norm_; }

  // Accessors for the expression data. Of course, if you call the wrong
  // function for the expression kind, you're gonna have a bad time.
  int byte() const;
  const pair<int, int>& byte_range() const;
  const list<Exp>& subexpressions() const;

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
Exp AnyByte();
Exp Byte(int byte);
Exp ByteRange(const pair<int, int>& byte_range);
Exp KleeneClosure(const list<Exp>& subexpressions, bool norm);
Exp Concatenation(const list<Exp>& subexpressions, bool norm);
Exp Complement(const list<Exp>& subexpressions, bool norm);
Exp Conjunction(const list<Exp>& subexpressions, bool norm);
Exp Disjunction(const list<Exp>& subexpressions, bool norm);

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

Exp AnyCharacter();
Exp Character(Rune character);
Exp CharacterClass(const set<Rune>& character_class);

// Returns the normalised form of exp.
Exp Normalised(Exp exp);

// Returns the nullability of exp as a bool.
// EmptySet and EmptyString map to false and true, respectively.
bool IsNullable(Exp exp);

// Returns the derivative of exp with respect to byte.
Exp Derivative(Exp exp, int byte);

// Outputs the partitions computed for exp.
// The first partition should be Σ-based. Any others should be ∅-based.
void Partitions(Exp exp, list<bitset<256>>* partitions);

// Outputs the expression parsed from str.
// Returns true on success, false on failure.
bool Parse(llvm::StringRef str, Exp* exp);

// Returns the result of matching str using exp.
bool Match(Exp exp, llvm::StringRef str);

// Represents a deterministic finite automaton.
struct DFA {
  map<pair<int, int>, int> transition_;
  map<int, bool> accepting_;
};

// Outputs the DFA compiled from exp.
// Returns the number of DFA states.
size_t Compile(Exp exp, DFA* dfa);

// Returns the result of matching str using dfa.
bool Match(const DFA& dfa, llvm::StringRef str);

// Represents a function and its machine code.
// Note that neither the function nor the machine code is owned.
struct Fun {
  llvm::Function* function_;
  void* machine_code_addr_;
  size_t machine_code_size_;
};

// Outputs the function compiled from dfa.
// Returns the number of bytes of machine code.
size_t Compile(const DFA& dfa, Fun* fun);

// Returns the result of matching str using fun.
bool Match(const Fun& fun, llvm::StringRef str);

// Deletes the internal state for fun.
void Delete(const Fun& fun);

}  // namespace redgrep

#endif  // REDGREP_REGEXP_H_
