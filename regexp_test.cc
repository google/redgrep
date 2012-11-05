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

#define EXPECT_ISNULLABLE(expected, exp) \
  do {                                   \
    if (expected) {                      \
      EXPECT_TRUE(IsNullable(exp));      \
    } else {                             \
      EXPECT_FALSE(IsNullable(exp));     \
    }                                    \
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

#define EXPECT_DERIVATIVE(expected, exp)                   \
  do {                                                     \
    EXPECT_EQ(expected, Normalised(Derivative(exp, 'a'))); \
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

#define EXPECT_PARTITIONS(expected, exp) \
  do {                                   \
    list<bitset<256>> partitions;        \
    Partitions(exp, &partitions);        \
    EXPECT_EQ(expected, partitions);     \
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

TEST(Parse, KleeneClosure) {
  EXPECT_PARSE(
      KleeneClosure(
          Byte('a')),
      "a*");
  EXPECT_PARSE(
      Concatenation(
          Byte('a'),
          KleeneClosure(
              Byte('a'))),
      "a+");
  EXPECT_PARSE(
      Disjunction(
          EmptyString(),
          Byte('a')),
      "a?");
  EXPECT_PARSE(
      KleeneClosure(
          KleeneClosure(
              Byte('a'))),
      "a**");
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
          Complement(
              Byte('a'))),
      "!!a");
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
          Conjunction(
              Byte('a'),
              Byte('b')),
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
          Disjunction(
              Byte('a'),
              Byte('b')),
          Byte('c')),
      "a|b|c");
}

#define EXPECT_MATCH(expected, str)  \
  do {                               \
    if (expected) {                  \
      EXPECT_TRUE(Match(exp, str));  \
      EXPECT_TRUE(Match(dfa, str));  \
      EXPECT_TRUE(Match(fun, str));  \
    } else {                         \
      EXPECT_FALSE(Match(exp, str)); \
      EXPECT_FALSE(Match(dfa, str)); \
      EXPECT_FALSE(Match(fun, str)); \
    }                                \
  } while (0)

TEST(Match, EmptySet) {
  Exp exp;
  DFA dfa;
  Fun fun;
  exp = EmptySet();
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(false, "a");
}

TEST(Match, EmptyString) {
  Exp exp;
  DFA dfa;
  Fun fun;
  exp = EmptyString();
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(false, "a");
}

TEST(Match, EscapeSequences) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("\\C", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  ASSERT_TRUE(Parse("\\f\\n\\r\\t", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "fnrt");
  EXPECT_MATCH(true, "\f\n\r\t");
  EXPECT_MATCH(false, "\\f\\n\\r\\t");
}

TEST(Match, AnyCharacter) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse(".", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(true, "¬");
  EXPECT_MATCH(true, "兔");
  EXPECT_MATCH(true, "💩");
}

TEST(Match, Character) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("a", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(false, "X");
  ASSERT_TRUE(Parse("¬", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "¬");
  EXPECT_MATCH(false, "X");
  ASSERT_TRUE(Parse("兔", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "兔");
  EXPECT_MATCH(false, "X");
  ASSERT_TRUE(Parse("💩", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "💩");
  EXPECT_MATCH(false, "X");
}

TEST(Match, CharacterClass) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("[a¬兔💩]", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(true, "¬");
  EXPECT_MATCH(true, "兔");
  EXPECT_MATCH(true, "💩");
  EXPECT_MATCH(false, "X");
  ASSERT_TRUE(Parse("[^a¬兔💩]", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(false, "a");
  EXPECT_MATCH(false, "¬");
  EXPECT_MATCH(false, "兔");
  EXPECT_MATCH(false, "💩");
  EXPECT_MATCH(true, "X");
}

TEST(Match, KleeneClosure) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("a*", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(true, "aa");
  ASSERT_TRUE(Parse("a+", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(true, "aa");
  ASSERT_TRUE(Parse("a?", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(false, "aa");
}

TEST(Match, Concatenation) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("aa", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(false, "a");
  EXPECT_MATCH(true, "aa");
  EXPECT_MATCH(false, "aaa");
}

TEST(Match, Complement) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("!a", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(false, "a");
  EXPECT_MATCH(true, "aa");
}

TEST(Match, Conjunction) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("a.&.b", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(false, "aa");
  EXPECT_MATCH(true, "ab");
  EXPECT_MATCH(false, "ba");
  EXPECT_MATCH(false, "bb");
}

TEST(Match, Disjunction) {
  Exp exp;
  DFA dfa;
  Fun fun;
  ASSERT_TRUE(Parse("a.|.b", &exp));
  Compile(exp, &dfa);
  Compile(dfa, &fun);
  EXPECT_MATCH(true, "aa");
  EXPECT_MATCH(true, "ab");
  EXPECT_MATCH(false, "ba");
  EXPECT_MATCH(true, "bb");
}

}  // namespace redgrep
