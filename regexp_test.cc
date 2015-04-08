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

#include "gtest/gtest.h"
#include "regexp.h"

namespace redgrep {

TEST(Compare, EmptySet) {
  EXPECT_EQ(
      EmptySet(),
      EmptySet());
}

TEST(Compare, EmptyString) {
  EXPECT_EQ(
      EmptyString(),
      EmptyString());
}

TEST(Compare, Group) {
  EXPECT_EQ(
      Group(0, Byte('a'), kPassive, true),
      Group(0, Byte('a'), kPassive, true));
  EXPECT_LT(
      Group(0, Byte('a'), kPassive, true),
      Group(1, Byte('a'), kPassive, true));
}

TEST(Compare, AnyByte) {
  EXPECT_EQ(
      AnyByte(),
      AnyByte());
}

TEST(Compare, Byte) {
  EXPECT_EQ(
      Byte('a'),
      Byte('a'));
  EXPECT_LT(
      Byte('a'),
      Byte('b'));
}

TEST(Compare, ByteRange) {
  EXPECT_EQ(
      ByteRange('a', 'c'),
      ByteRange('a', 'c'));
  EXPECT_LT(
      ByteRange('a', 'c'),
      ByteRange('b', 'd'));
}

TEST(Compare, KleeneClosure) {
  EXPECT_EQ(
      KleeneClosure(Byte('a')),
      KleeneClosure(Byte('a')));
  EXPECT_LT(
      KleeneClosure(Byte('a')),
      KleeneClosure(Byte('b')));
}

TEST(Compare, Concatenation) {
  EXPECT_EQ(
      Concatenation(Byte('a'), Byte('b'), Byte('c')),
      Concatenation(Byte('a'), Byte('b'), Byte('c')));
  EXPECT_LT(
      Concatenation(Byte('a'), Byte('b'), Byte('c')),
      Concatenation(Byte('b'), Byte('c'), Byte('d')));
}

TEST(Compare, Complement) {
  EXPECT_EQ(
      Complement(Byte('a')),
      Complement(Byte('a')));
  EXPECT_LT(
      Complement(Byte('a')),
      Complement(Byte('b')));
}

TEST(Compare, Conjunction) {
  EXPECT_EQ(
      Conjunction(Byte('a'), Byte('b'), Byte('c')),
      Conjunction(Byte('a'), Byte('b'), Byte('c')));
  EXPECT_LT(
      Conjunction(Byte('a'), Byte('b'), Byte('c')),
      Conjunction(Byte('b'), Byte('c'), Byte('d')));
}

TEST(Compare, Disjunction) {
  EXPECT_EQ(
      Disjunction(Byte('a'), Byte('b'), Byte('c')),
      Disjunction(Byte('a'), Byte('b'), Byte('c')));
  EXPECT_LT(
      Disjunction(Byte('a'), Byte('b'), Byte('c')),
      Disjunction(Byte('b'), Byte('c'), Byte('d')));
}

#define EXPECT_NORMALISED(expected, exp)  \
  do {                                    \
    EXPECT_EQ(expected, Normalised(exp)); \
  } while (0)

TEST(Normalised, EmptySet) {
  EXPECT_NORMALISED(
      EmptySet(),
      EmptySet());
}

TEST(Normalised, EmptyString) {
  EXPECT_NORMALISED(
      EmptyString(),
      EmptyString());
}

TEST(Normalised, Group) {
  EXPECT_NORMALISED(
      EmptySet(),
      Group(0, EmptySet(), kPassive, true));
  EXPECT_NORMALISED(
      EmptyString(),
      Group(0, EmptyString(), kPassive, true));
  EXPECT_NORMALISED(
      Group(0, Byte('a'), kPassive, true),
      Group(0, Byte('a'), kPassive, true));
}

TEST(Normalised, AnyByte) {
  EXPECT_NORMALISED(
      AnyByte(),
      AnyByte());
}

TEST(Normalised, Byte) {
  EXPECT_NORMALISED(
      Byte('a'),
      Byte('a'));
}

TEST(Normalised, ByteRange) {
  EXPECT_NORMALISED(
      ByteRange('a', 'c'),
      ByteRange('a', 'c'));
}

TEST(Normalised, KleeneClosure) {
  EXPECT_NORMALISED(
      KleeneClosure(Byte('a')),
      KleeneClosure(KleeneClosure(Byte('a'))));
  EXPECT_NORMALISED(
      EmptyString(),
      KleeneClosure(EmptySet()));
  EXPECT_NORMALISED(
      EmptyString(),
      KleeneClosure(EmptyString()));
  EXPECT_NORMALISED(
      Complement(EmptySet()),
      KleeneClosure(AnyByte()));
  EXPECT_NORMALISED(
      Complement(EmptySet()),
      KleeneClosure(AnyCharacter()));
}

TEST(Normalised, Concatenation) {
  EXPECT_NORMALISED(
      Concatenation(
          Byte('a'),
          Concatenation(
              Byte('b'),
              Byte('c'))),
      Concatenation(
          Concatenation(
              Byte('a'),
              Byte('b')),
          Byte('c')));
  EXPECT_NORMALISED(
      EmptySet(),
      Concatenation(EmptySet(), Byte('a')));
  EXPECT_NORMALISED(
      EmptySet(),
      Concatenation(Byte('a'), EmptySet()));
  EXPECT_NORMALISED(
      Byte('a'),
      Concatenation(EmptyString(), Byte('a')));
  EXPECT_NORMALISED(
      Byte('a'),
      Concatenation(Byte('a'), EmptyString()));
}

TEST(Normalised, Complement) {
  EXPECT_NORMALISED(
      Byte('a'),
      Complement(Complement(Byte('a'))));
}

TEST(Normalised, Conjunction) {
  EXPECT_NORMALISED(
      Conjunction(
          Byte('a'),
          Byte('b'),
          Byte('c')),
      Conjunction(
          Conjunction(
              Byte('a'),
              Byte('b')),
          Byte('c')));
  EXPECT_NORMALISED(
      Conjunction(Byte('a'), Byte('b')),
      Conjunction(Byte('b'), Byte('a')));
  EXPECT_NORMALISED(
      Byte('a'),
      Conjunction(Byte('a'), Byte('a')));
  EXPECT_NORMALISED(
      EmptySet(),
      Conjunction(Byte('a'), EmptySet()));
  EXPECT_NORMALISED(
      Byte('a'),
      Conjunction(Byte('a'), Complement(EmptySet())));
}

TEST(Normalised, Disjunction) {
  EXPECT_NORMALISED(
      Disjunction(
          Byte('a'),
          Byte('b'),
          Byte('c')),
      Disjunction(
          Disjunction(
              Byte('a'),
              Byte('b')),
          Byte('c')));
  EXPECT_NORMALISED(
      Disjunction(Byte('a'), Byte('b')),
      Disjunction(Byte('b'), Byte('a')));
  EXPECT_NORMALISED(
      Byte('a'),
      Disjunction(Byte('a'), Byte('a')));
  EXPECT_NORMALISED(
      Byte('a'),
      Disjunction(Byte('a'), EmptySet()));
  EXPECT_NORMALISED(
      Complement(EmptySet()),
      Disjunction(Byte('a'), Complement(EmptySet())));
}

#define EXPECT_ISNULLABLE(expected, exp)  \
  do {                                    \
    if (expected) {                       \
      EXPECT_TRUE(IsNullable(exp));       \
    } else {                              \
      EXPECT_FALSE(IsNullable(exp));      \
    }                                     \
  } while (0)

TEST(IsNullable, EmptySet) {
  EXPECT_ISNULLABLE(
      false,
      EmptySet());
}

TEST(IsNullable, EmptyString) {
  EXPECT_ISNULLABLE(
      true,
      EmptyString());
}

TEST(IsNullable, Group) {
  EXPECT_ISNULLABLE(
      false,
      Group(0, Byte('a'), kPassive, true));
}

TEST(IsNullable, AnyByte) {
  EXPECT_ISNULLABLE(
      false,
      AnyByte());
}

TEST(IsNullable, Byte) {
  EXPECT_ISNULLABLE(
      false,
      Byte('a'));
}

TEST(IsNullable, ByteRange) {
  EXPECT_ISNULLABLE(
      false,
      ByteRange('a', 'c'));
}

TEST(IsNullable, KleeneClosure) {
  EXPECT_ISNULLABLE(
      true,
      KleeneClosure(Byte('a')));
}

TEST(IsNullable, Concatenation) {
  EXPECT_ISNULLABLE(
      false,
      Concatenation(Byte('a'), Byte('b')));
}

TEST(IsNullable, Complement) {
  EXPECT_ISNULLABLE(
      true,
      Complement(Byte('a')));
}

TEST(IsNullable, Conjunction) {
  EXPECT_ISNULLABLE(
      false,
      Conjunction(Byte('a'), Byte('b')));
}

TEST(IsNullable, Disjunction) {
  EXPECT_ISNULLABLE(
      false,
      Disjunction(Byte('a'), Byte('b')));
}

#define EXPECT_DERIVATIVE(expected, exp)                    \
  do {                                                      \
    EXPECT_EQ(expected, Normalised(Derivative(exp, 'a')));  \
  } while (0)

TEST(Derivative, EmptySet) {
  EXPECT_DERIVATIVE(
      EmptySet(),
      EmptySet());
}

TEST(Derivative, EmptyString) {
  EXPECT_DERIVATIVE(
      EmptySet(),
      EmptyString());
}

TEST(Derivative, Group) {
  // This should never happen.
}

TEST(Derivative, AnyByte) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      AnyByte());
}

TEST(Derivative, Byte) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      Byte('a'));
  EXPECT_DERIVATIVE(
      EmptySet(),
      Byte('b'));
}

TEST(Derivative, ByteRange) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      ByteRange('a', 'c'));
  EXPECT_DERIVATIVE(
      EmptySet(),
      ByteRange('b', 'd'));
}

TEST(Derivative, KleeneClosure) {
  EXPECT_DERIVATIVE(
      KleeneClosure(Byte('a')),
      KleeneClosure(Byte('a')));
}

TEST(Derivative, Concatenation) {
  EXPECT_DERIVATIVE(
      Byte('b'),
      Concatenation(Byte('a'), Byte('b')));
  EXPECT_DERIVATIVE(
      Concatenation(KleeneClosure(Byte('a')), Byte('b')),
      Concatenation(KleeneClosure(Byte('a')), Byte('b')));
}

TEST(Derivative, Complement) {
  EXPECT_DERIVATIVE(
      Complement(EmptyString()),
      Complement(Byte('a')));
}

TEST(Derivative, Conjunction) {
  EXPECT_DERIVATIVE(
      EmptySet(),
      Conjunction(Byte('a'), Byte('b')));
}

TEST(Derivative, Disjunction) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      Disjunction(Byte('a'), Byte('b')));
}

#define EXPECT_OUTERSET(expected, outer)  \
  do {                                    \
    list<Exp> subs;                       \
    for (const auto& i : *outer) {        \
      subs.push_back(i.first);            \
    }                                     \
    Exp exp = Disjunction(subs, false);   \
    EXPECT_EQ(expected, Normalised(exp)); \
  } while (0)

TEST(OuterSet, PartialConcatenation) {
  Outer outer = PartialConcatenation(
      Denormalised(
          Disjunction(
              Conjunction(Byte('1'), Byte('2')),
              Byte('3'))),
      Byte('4'),
      Bindings({}));
  EXPECT_OUTERSET(
      Disjunction(
          Concatenation(Byte('3'), Byte('4')),
          Conjunction(
              Concatenation(Byte('1'), Byte('4')),
              Concatenation(Byte('2'), Byte('4')))),
      outer);
}

TEST(OuterSet, PartialComplement) {
  Outer outer = PartialComplement(
      Denormalised(
          Disjunction(
              Conjunction(Byte('1'), Byte('2')),
              Byte('3'))));
  EXPECT_OUTERSET(
      Disjunction(
          Conjunction(
              Complement(Byte('1')),
              Complement(Byte('3'))),
          Conjunction(
              Complement(Byte('2')),
              Complement(Byte('3')))),
      outer);
}

TEST(OuterSet, PartialConjunction) {
  Outer outer = PartialConjunction(
      Denormalised(
          Disjunction(Byte('1'), Byte('2'))),
      Denormalised(
          Disjunction(Byte('3'), Byte('4'))));
  EXPECT_OUTERSET(
      Disjunction(
          Conjunction(Byte('1'), Byte('3')),
          Conjunction(Byte('1'), Byte('4')),
          Conjunction(Byte('2'), Byte('3')),
          Conjunction(Byte('2'), Byte('4'))),
      outer);
}

TEST(OuterSet, PartialDisjunction) {
  Outer outer = PartialDisjunction(
      Denormalised(
          Disjunction(Byte('1'), Byte('2'))),
      Denormalised(
          Disjunction(Byte('3'), Byte('4'))));
  EXPECT_OUTERSET(
      Disjunction(Byte('1'), Byte('2'), Byte('3'), Byte('4')),
      outer);
}

#define EXPECT_PARTIAL(expected, exp) \
  do {                                \
    Outer outer = Partial(exp, 'a');  \
    EXPECT_OUTERSET(expected, outer); \
  } while (0)

TEST(Partial, EmptySet) {
  EXPECT_PARTIAL(
      EmptySet(),
      EmptySet());
}

TEST(Partial, EmptyString) {
  EXPECT_PARTIAL(
      EmptySet(),
      EmptyString());
}

TEST(Partial, Group) {
  EXPECT_PARTIAL(
      EmptyString(),
      Group(0, Byte('a'), kPassive, true));
}

TEST(Partial, AnyByte) {
  EXPECT_PARTIAL(
      EmptyString(),
      AnyByte());
}

TEST(Partial, Byte) {
  EXPECT_PARTIAL(
      EmptyString(),
      Byte('a'));
  EXPECT_PARTIAL(
      EmptySet(),
      Byte('b'));
}

TEST(Partial, ByteRange) {
  EXPECT_PARTIAL(
      EmptyString(),
      ByteRange('a', 'c'));
  EXPECT_PARTIAL(
      EmptySet(),
      ByteRange('b', 'd'));
}

TEST(Partial, KleeneClosure) {
  EXPECT_PARTIAL(
      KleeneClosure(Byte('a')),
      KleeneClosure(Byte('a')));
}

TEST(Partial, Concatenation) {
  EXPECT_PARTIAL(
      Byte('b'),
      Concatenation(Byte('a'), Byte('b')));
  EXPECT_PARTIAL(
      Concatenation(KleeneClosure(Byte('a')), Byte('b')),
      Concatenation(KleeneClosure(Byte('a')), Byte('b')));
}

TEST(Partial, Complement) {
  EXPECT_PARTIAL(
      Complement(EmptyString()),
      Complement(Byte('a')));
}

TEST(Partial, Conjunction) {
  EXPECT_PARTIAL(
      EmptySet(),
      Conjunction(Byte('a'), Byte('b')));
}

TEST(Partial, Disjunction) {
  EXPECT_PARTIAL(
      EmptyString(),
      Disjunction(Byte('a'), Byte('b')));
}

#define EXPECT_PARTITIONS(expected, exp)  \
  do {                                    \
    list<bitset<256>> partitions;         \
    Partitions(exp, &partitions);         \
    EXPECT_EQ(expected, partitions);      \
  } while (0)

template <typename... Variadic>
inline bitset<256> BitSet(Variadic... bits) {
  set<int> s({bits...});
  bitset<256> bs;
  for (int bit : s) {
    bs.set(bit);
  }
  return bs;
}

TEST(Partitions, EmptySet) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet()}),
      EmptySet());
}

TEST(Partitions, EmptyString) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet()}),
      EmptyString());
}

TEST(Partitions, Group) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a'),
                         BitSet('a')}),
      Group(0, Byte('a'), kPassive, true));
}

TEST(Partitions, AnyByte) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet()}),
      AnyByte());
}

TEST(Partitions, Byte) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a'),
                         BitSet('a')}),
      Byte('a'));
}

TEST(Partitions, ByteRange) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a', 'b', 'c'),
                         BitSet('a', 'b', 'c')}),
      ByteRange('a', 'c'));
}

TEST(Partitions, KleeneClosure) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a'),
                         BitSet('a')}),
      KleeneClosure(Byte('a')));
}

TEST(Partitions, Concatenation) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a'),
                         BitSet('a')}),
      Concatenation(Byte('a'), Byte('b')));
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a', 'b'),
                         BitSet('b'),
                         BitSet('a')}),
      Concatenation(KleeneClosure(Byte('a')), Byte('b')));
}

TEST(Partitions, Complement) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a'),
                         BitSet('a')}),
      Complement(Byte('a')));
}

TEST(Partitions, Conjunction) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a', 'b'),
                         BitSet('b'),
                         BitSet('a')}),
      Conjunction(Byte('a'), Byte('b')));
}

TEST(Partitions, Disjunction) {
  EXPECT_PARTITIONS(
      list<bitset<256>>({BitSet('a', 'b'),
                         BitSet('b'),
                         BitSet('a')}),
      Disjunction(Byte('a'), Byte('b')));
}

#define EXPECT_PARSE(expected, str) \
  do {                              \
    Exp exp;                        \
    ASSERT_TRUE(Parse(str, &exp));  \
    EXPECT_EQ(expected, exp);       \
  } while (0)

TEST(Parse, EscapeSequences) {
  EXPECT_PARSE(
      AnyByte(),
      "\\C");
  EXPECT_PARSE(
      Concatenation(
          Byte('\f'),
          Byte('\n'),
          Byte('\r'),
          Byte('\t')),
      "\\f\\n\\r\\t");
}

TEST(Parse, AnyCharacter) {
  EXPECT_PARSE(
      Disjunction(
          ByteRange(0x00, 0x7F),
          Concatenation(
              ByteRange(0xC0, 0xDF),
              ByteRange(0x80, 0xBF)),
          Concatenation(
              ByteRange(0xE0, 0xEF),
              ByteRange(0x80, 0xBF),
              ByteRange(0x80, 0xBF)),
          Concatenation(
              ByteRange(0xF0, 0xF7),
              ByteRange(0x80, 0xBF),
              ByteRange(0x80, 0xBF),
              ByteRange(0x80, 0xBF))),
      ".");
}

TEST(Parse, Character) {
  EXPECT_PARSE(
      Byte(0x61),
      "a");
  EXPECT_PARSE(
      Concatenation(
          Byte(0xC2),
          Byte(0xAC)),
      "¬");
  EXPECT_PARSE(
      Concatenation(
          Byte(0xE5),
          Byte(0x85),
          Byte(0x94)),
      "兔");
  EXPECT_PARSE(
      Concatenation(
          Byte(0xF0),
          Byte(0x9F),
          Byte(0x92),
          Byte(0xA9)),
      "💩");
}

TEST(Parse, CharacterClass) {
  EXPECT_PARSE(
      Disjunction(
          Byte(0x61),
          Concatenation(
              Byte(0xC2),
              Byte(0xAC)),
          Concatenation(
              Byte(0xE5),
              Byte(0x85),
              Byte(0x94)),
          Concatenation(
              Byte(0xF0),
              Byte(0x9F),
              Byte(0x92),
              Byte(0xA9))),
      "[a¬兔💩]");
  EXPECT_PARSE(
      Conjunction(
          Complement(
              Disjunction(
                  Byte(0x61),
                  Concatenation(
                      Byte(0xC2),
                      Byte(0xAC)),
                  Concatenation(
                      Byte(0xE5),
                      Byte(0x85),
                      Byte(0x94)),
                  Concatenation(
                      Byte(0xF0),
                      Byte(0x9F),
                      Byte(0x92),
                      Byte(0xA9)))),
          AnyCharacter()),
      "[^a¬兔💩]");
}

TEST(Parse, Quantifiers) {
  EXPECT_PARSE(
      KleeneClosure(
          Byte('a')),
      "a*");
  EXPECT_PARSE(
      KleeneClosure(
          Byte('a')),
      "a*?");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          KleeneClosure(
              Byte('a'))),
      "a+");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          KleeneClosure(
              Byte('a'))),
      "a+?");
  EXPECT_PARSE(
      Disjunction(
          EmptyString(),
          Byte('a')),
      "a?");
  EXPECT_PARSE(
      Disjunction(
          EmptyString(),
          Byte('a')),
      "a??");
  EXPECT_PARSE(
      Byte('a'),
      "a{1}");
  EXPECT_PARSE(
      Byte('a'),
      "a{1}?");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          KleeneClosure(
              Byte('a'))),
      "a{1,}");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          KleeneClosure(
              Byte('a'))),
      "a{1,}?");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          Disjunction(
              EmptyString(),
              Byte('a'))),
      "a{1,2}");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          Disjunction(
              EmptyString(),
              Byte('a'))),
      "a{1,2}?");
}

TEST(Parse, KleeneClosure) {
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          KleeneClosure(
              Byte('b'))),
      "ab*");
  EXPECT_PARSE(
      KleeneClosure(
          Concatenation(
              Byte('a'),
              Byte('b'))),
      "(ab)*");
  EXPECT_PARSE(
      Concatenation(
          KleeneClosure(
              Byte('a')),
          Byte('b')),
      "a*b");
  EXPECT_PARSE(
      Concatenation(
          KleeneClosure(
              Byte('a')),
          Concatenation(
              KleeneClosure(
                  Byte('b')),
              Byte('c'))),
      "a*b*c");
}

TEST(Parse, Concatenation) {
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          Byte('b')),
      "ab");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          Concatenation(
              Byte('b'),
              Byte('c'))),
      "abc");
}

TEST(Parse, Complement) {
  EXPECT_PARSE(
      Complement(
          Byte('a')),
      "!a");
  EXPECT_PARSE(
      Complement(
          Concatenation(
              Byte('a'),
              Byte('b'))),
      "!ab");
  EXPECT_PARSE(
      Complement(
          Concatenation(
              Byte('a'),
              Byte('b'))),
      "!(ab)");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          Complement(
              Byte('b'))),
      "a!b");
  EXPECT_PARSE(
      Concatenation(
          Concatenation(
              Byte('a'),
              Complement(
                  Byte('b'))),
          Complement(
              Byte('c'))),
      "a!b!c");
}

TEST(Parse, Conjunction) {
  EXPECT_PARSE(
      Conjunction(
          Byte('a'),
          Byte('b')),
      "a&b");
  EXPECT_PARSE(
      Conjunction(
          Byte('a'),
          Byte('b'),
          Byte('c')),
      "a&b&c");
}

TEST(Parse, Disjunction) {
  EXPECT_PARSE(
      Disjunction(
          Byte('a'),
          Byte('b')),
      "a|b");
  EXPECT_PARSE(
      Disjunction(
          Byte('a'),
          Byte('b'),
          Byte('c')),
      "a|b|c");
}

TEST(Parse, CountedRepetition) {
  Exp exp1;
  EXPECT_TRUE(Parse("a{0}", &exp1));
  EXPECT_EQ(EmptyString(), exp1);

  Exp exp2;
  EXPECT_TRUE(Parse("a{1000}", &exp2));
  Exp exp3;
  EXPECT_TRUE(Parse("a{2}{2}{2}{5}{5}{5}", &exp3));
  // They are structured differently, so compare their normalised forms.
  EXPECT_EQ(Normalised(exp2), Normalised(exp3));

  Exp exp4;
  EXPECT_FALSE(Parse("a{1001}", &exp4));
  EXPECT_FALSE(Parse("a{7}{11}{13}", &exp4));

  Exp exp5;
  EXPECT_FALSE(Parse("a{999999999}", &exp5));
  EXPECT_FALSE(Parse("a{10}{10}{10}{10}{10}{10}{10}{10}{10}{10}", &exp5));
}

#define EXPECT_PARSE_M_C(expected, expected_modes, expected_captures, str)  \
  do {                                                                      \
    Exp exp;                                                                \
    vector<Mode> modes;                                                     \
    vector<int> captures;                                                   \
    ASSERT_TRUE(Parse(str, &exp, &modes, &captures));                       \
    EXPECT_EQ(expected, exp);                                               \
    EXPECT_EQ(expected_modes, modes);                                       \
    EXPECT_EQ(expected_captures, captures);                                 \
  } while (0)

TEST(Parse_M_C, Parentheses) {
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                Byte('b')),
            kPassive, false),
      vector<Mode>({kPassive}),
      vector<int>({}),
      "(?:ab)");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                Byte('b')),
            kPassive, true),
      vector<Mode>({kPassive}),
      vector<int>({0}),
      "(ab)");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Group(1,
                      Byte('a'),
                      kPassive, true),
                Byte('b')),
            kPassive, true),
      vector<Mode>({kPassive, kPassive}),
      vector<int>({0, 1}),
      "((a)b)");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                Group(1,
                      Byte('b'),
                      kPassive, true)),
            kPassive, true),
      vector<Mode>({kPassive, kPassive}),
      vector<int>({0, 1}),
      "(a(b))");
  EXPECT_PARSE_M_C(
      Concatenation(
          Group(0,
                Byte('a'),
                kPassive, true),
          Group(1,
                Byte('b'),
                kPassive, true)),
      vector<Mode>({kPassive, kPassive}),
      vector<int>({0, 1}),
      "(a)(b)");
}

TEST(Parse_M_C, Quantifiers) {
  EXPECT_PARSE_M_C(
      Group(0,
            KleeneClosure(Byte('a')),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "a*");
  EXPECT_PARSE_M_C(
      Group(0,
            KleeneClosure(Byte('a')),
            kMinimal, false),
      vector<Mode>({kMinimal}),
      vector<int>({}),
      "a*?");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                KleeneClosure(Byte('a'))),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "a+");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                KleeneClosure(Byte('a'))),
            kMinimal, false),
      vector<Mode>({kMinimal}),
      vector<int>({}),
      "a+?");
  EXPECT_PARSE_M_C(
      Group(0,
            Disjunction(
                EmptyString(),
                Byte('a')),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "a?");
  EXPECT_PARSE_M_C(
      Group(0,
            Disjunction(
                EmptyString(),
                Byte('a')),
            kMinimal, false),
      vector<Mode>({kMinimal}),
      vector<int>({}),
      "a??");
  EXPECT_PARSE_M_C(
      Group(0,
            Byte('a'),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "a{1}");
  EXPECT_PARSE_M_C(
      Group(0,
            Byte('a'),
            kMinimal, false),
      vector<Mode>({kMinimal}),
      vector<int>({}),
      "a{1}?");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                KleeneClosure(Byte('a'))),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "a{1,}");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                KleeneClosure(Byte('a'))),
            kMinimal, false),
      vector<Mode>({kMinimal}),
      vector<int>({}),
      "a{1,}?");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                Disjunction(
                    EmptyString(),
                    Byte('a'))),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "a{1,2}");
  EXPECT_PARSE_M_C(
      Group(0,
            Concatenation(
                Byte('a'),
                Disjunction(
                    EmptyString(),
                    Byte('a'))),
            kMinimal, false),
      vector<Mode>({kMinimal}),
      vector<int>({}),
      "a{1,2}?");
}

TEST(Parse_M_C, ApplyGroups) {
  EXPECT_PARSE_M_C(
      AnyCharacter(),
      vector<Mode>({}),
      vector<int>({}),
      ".");
  EXPECT_PARSE_M_C(
      Disjunction(
          Byte('a'),
          Byte('b'),
          Byte('c')),
      vector<Mode>({}),
      vector<int>({}),
      "[abc]");
  EXPECT_PARSE_M_C(
      Conjunction(
          Complement(
              Disjunction(
                  Byte('a'),
                  Byte('b'),
                  Byte('c'))),
          AnyCharacter()),
      vector<Mode>({}),
      vector<int>({}),
      "[^abc]");
  EXPECT_PARSE_M_C(
      Disjunction(
          Group(0,
                Concatenation(Byte('a'), Byte('a'), Byte('a')),
                kPassive, false),
          Group(1,
                Concatenation(Byte('b'), Byte('b'), Byte('b')),
                kPassive, false),
          Group(2,
                Concatenation(Byte('c'), Byte('c'), Byte('c')),
                kPassive, false)),
      vector<Mode>({kPassive, kPassive, kPassive}),
      vector<int>({}),
      "aaa|bbb|ccc");
  EXPECT_PARSE_M_C(
      Group(0,
            Complement(
                Concatenation(
                    Byte('a'),
                    Byte('b'),
                    Byte('c'))),
            kMaximal, false),
      vector<Mode>({kMaximal}),
      vector<int>({}),
      "!abc");
}

#define EXPECT_MATCH(expected, expected_values, str)  \
  do {                                                \
    vector<int> values;                               \
    if (expected) {                                   \
      EXPECT_TRUE(Match(exp1_, str));                 \
      EXPECT_TRUE(Match(dfa_, str));                  \
      EXPECT_TRUE(Match(fun1_, str));                 \
      EXPECT_TRUE(Match(tnfa_, str, &values));        \
      EXPECT_EQ(expected_values, values);             \
    } else {                                          \
      EXPECT_FALSE(Match(exp1_, str));                \
      EXPECT_FALSE(Match(dfa_, str));                 \
      EXPECT_FALSE(Match(fun1_, str));                \
      EXPECT_FALSE(Match(tnfa_, str, &values));       \
    }                                                 \
  } while (0)

class MatchTest : public testing::Test {
 protected:
  void ParseAll(llvm::StringRef str) {
    ASSERT_TRUE(Parse(str, &exp1_));
    ASSERT_TRUE(Parse(str, &exp2_, &tnfa_.modes_, &tnfa_.captures_));
  }

  void CompileAll() {
    Compile(exp1_, &dfa_);
    Compile(dfa_, &fun1_);
    Compile(exp2_, &tnfa_);
  }

  Exp exp1_;
  DFA dfa_;
  Fun fun1_;

  Exp exp2_;
  TNFA tnfa_;
};

TEST_F(MatchTest, EmptySet) {
  exp1_ = exp2_ = EmptySet();
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
}

TEST_F(MatchTest, EmptyString) {
  exp1_ = exp2_ = EmptyString();
  CompileAll();
  EXPECT_MATCH(true, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
}

TEST_F(MatchTest, EscapeSequences_1) {
  ParseAll("(\\C)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
}

TEST_F(MatchTest, EscapeSequences_2) {
  ParseAll("(\\f\\n\\r\\t)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "fnrt");
  EXPECT_MATCH(true, vector<int>({0, 4}), "\f\n\r\t");
  EXPECT_MATCH(false, vector<int>({}), "\\f\\n\\r\\t");
}

TEST_F(MatchTest, AnyCharacter) {
  ParseAll("(.)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "¬");
  EXPECT_MATCH(true, vector<int>({0, 3}), "兔");
  EXPECT_MATCH(true, vector<int>({0, 4}), "💩");
}

TEST_F(MatchTest, Character_1) {
  ParseAll("(a)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(false, vector<int>({}), "X");
}

TEST_F(MatchTest, Character_2) {
  ParseAll("(¬)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 2}), "¬");
  EXPECT_MATCH(false, vector<int>({}), "X");
}

TEST_F(MatchTest, Character_3) {
  ParseAll("(兔)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 3}), "兔");
  EXPECT_MATCH(false, vector<int>({}), "X");
}

TEST_F(MatchTest, Character_4) {
  ParseAll("(💩)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 4}), "💩");
  EXPECT_MATCH(false, vector<int>({}), "X");
}

TEST_F(MatchTest, CharacterClass_1) {
  ParseAll("([a¬兔💩])");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "¬");
  EXPECT_MATCH(true, vector<int>({0, 3}), "兔");
  EXPECT_MATCH(true, vector<int>({0, 4}), "💩");
  EXPECT_MATCH(false, vector<int>({}), "X");
}

TEST_F(MatchTest, CharacterClass_2) {
  ParseAll("([^a¬兔💩])");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(false, vector<int>({}), "¬");
  EXPECT_MATCH(false, vector<int>({}), "兔");
  EXPECT_MATCH(false, vector<int>({}), "💩");
  EXPECT_MATCH(true, vector<int>({0, 1}), "X");
}

TEST_F(MatchTest, Quantifiers_1) {
  ParseAll("(a*)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_2) {
  ParseAll("(a*)(a*)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2, 2, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3, 3, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_3) {
  ParseAll("(a*?)(a*)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_4) {
  ParseAll("(a+)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_5) {
  ParseAll("(a+)(a+)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, 2, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_6) {
  ParseAll("(a+?)(a+)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_7) {
  ParseAll("(a?)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(false, vector<int>({}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_8) {
  ParseAll("(a?)(a?)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_9) {
  ParseAll("(a?""?)(a?)");  // Avoid trigraph.
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 0, 0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_10) {
  ParseAll("(a{1})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(false, vector<int>({}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_11) {
  ParseAll("(a{1})(a{1})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_12) {
  ParseAll("(a{1}?)(a{1})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_13) {
  ParseAll("(a{1,})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_14) {
  ParseAll("(a{1,})(a{1,})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, 2, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_15) {
  ParseAll("(a{1,}?)(a{1,})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_16) {
  ParseAll("(a{1,2})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(true, vector<int>({0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Quantifiers_17) {
  ParseAll("(a{1,2})(a{1,2})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, 2, 3}), "aaa");
}

TEST_F(MatchTest, Quantifiers_18) {
  ParseAll("(a{1,2}?)(a{1,2})");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 1, 1, 3}), "aaa");
}

TEST_F(MatchTest, Concatenation) {
  ParseAll("(aa)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "aa");
  EXPECT_MATCH(false, vector<int>({}), "aaa");
}

TEST_F(MatchTest, Complement_1) {
  ParseAll("(!a)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3}), "aaa");
}

TEST_F(MatchTest, Complement_2) {
  ParseAll("(!(a))");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, -1, -1}), "");
  EXPECT_MATCH(false, vector<int>({}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2, -1, -1}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3, -1, -1}), "aaa");
}

TEST_F(MatchTest, Conjunction_1) {
  ParseAll("(a.)&(.b)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, 0, 2}), "ab");
  EXPECT_MATCH(false, vector<int>({}), "ba");
  EXPECT_MATCH(false, vector<int>({}), "bb");
}

TEST_F(MatchTest, Conjunction_2) {
  ParseAll("(a.*)&(.*b)");
  CompileAll();
  EXPECT_MATCH(false, vector<int>({}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, 0, 2}), "ab");
  EXPECT_MATCH(false, vector<int>({}), "ba");
  EXPECT_MATCH(false, vector<int>({}), "bb");
  EXPECT_MATCH(false, vector<int>({}), "aXa");
  EXPECT_MATCH(true, vector<int>({0, 3, 0, 3}), "aXb");
  EXPECT_MATCH(false, vector<int>({}), "bXa");
  EXPECT_MATCH(false, vector<int>({}), "bXb");
}

TEST_F(MatchTest, Disjunction_1) {
  ParseAll("(a.)|(.b)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 2, -1, -1}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, -1, -1}), "ab");
  EXPECT_MATCH(false, vector<int>({}), "ba");
  EXPECT_MATCH(true, vector<int>({-1, -1, 0, 2}), "bb");
}

TEST_F(MatchTest, Disjunction_2) {
  ParseAll("(a.*)|(.*b)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 2, -1, -1}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 2, -1, -1}), "ab");
  EXPECT_MATCH(false, vector<int>({}), "ba");
  EXPECT_MATCH(true, vector<int>({-1, -1, 0, 2}), "bb");
  EXPECT_MATCH(true, vector<int>({0, 3, -1, -1}), "aXa");
  EXPECT_MATCH(true, vector<int>({0, 3, -1, -1}), "aXb");
  EXPECT_MATCH(false, vector<int>({}), "bXa");
  EXPECT_MATCH(true, vector<int>({-1, -1, 0, 3}), "bXb");
}

TEST_F(MatchTest, PerlSemantics_1) {
  ParseAll("(?:(a*?)|(a*))(a*)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, -1, -1, 0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 0, -1, -1, 0, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 0, -1, -1, 0, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 0, -1, -1, 0, 3}), "aaa");
}

TEST_F(MatchTest, PerlSemantics_2) {
  ParseAll("(?:(a*)|(a*?))(a*)");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({0, 0, -1, -1, 0, 0}), "");
  EXPECT_MATCH(true, vector<int>({0, 1, -1, -1, 1, 1}), "a");
  EXPECT_MATCH(true, vector<int>({0, 2, -1, -1, 2, 2}), "aa");
  EXPECT_MATCH(true, vector<int>({0, 3, -1, -1, 3, 3}), "aaa");
}

// http://swtch.com/~rsc/regexp/regexp2.html#posix
TEST_F(MatchTest, PerlSemantics_3) {
  ParseAll("(a|bcdef|g|ab|c|d|e|efg|fg)*");
  CompileAll();
  EXPECT_MATCH(true, vector<int>({6, 7}), "abcdefg");
}

}  // namespace redgrep
