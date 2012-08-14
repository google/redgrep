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

TEST(Compare, AnyCharacter) {
  EXPECT_EQ(
      AnyCharacter(),
      AnyCharacter());
}

TEST(Compare, Character) {
  EXPECT_EQ(
      Character('a'),
      Character('a'));
  EXPECT_LT(
      Character('a'),
      Character('b'));
}

TEST(Compare, CharacterClass) {
  EXPECT_EQ(
      CharacterClass({'a', 'b', 'c'}),
      CharacterClass({'a', 'b', 'c'}));
  EXPECT_LT(
      CharacterClass({'a', 'b', 'c'}),
      CharacterClass({'b', 'c', 'd'}));
}

TEST(Compare, KleeneClosure) {
  EXPECT_EQ(
      KleeneClosure(Character('a')),
      KleeneClosure(Character('a')));
  EXPECT_LT(
      KleeneClosure(Character('a')),
      KleeneClosure(Character('b')));
}

TEST(Compare, Concatenation) {
  EXPECT_EQ(
      Concatenation(Character('a'), Character('b'), Character('c')),
      Concatenation(Character('a'), Character('b'), Character('c')));
  EXPECT_LT(
      Concatenation(Character('a'), Character('b'), Character('c')),
      Concatenation(Character('b'), Character('c'), Character('d')));
}

TEST(Compare, Complement) {
  EXPECT_EQ(
      Complement(Character('a')),
      Complement(Character('a')));
  EXPECT_LT(
      Complement(Character('a')),
      Complement(Character('b')));
}

TEST(Compare, Conjunction) {
  EXPECT_EQ(
      Conjunction(Character('a'), Character('b'), Character('c')),
      Conjunction(Character('a'), Character('b'), Character('c')));
  EXPECT_LT(
      Conjunction(Character('a'), Character('b'), Character('c')),
      Conjunction(Character('b'), Character('c'), Character('d')));
}

TEST(Compare, Disjunction) {
  EXPECT_EQ(
      Disjunction(Character('a'), Character('b'), Character('c')),
      Disjunction(Character('a'), Character('b'), Character('c')));
  EXPECT_LT(
      Disjunction(Character('a'), Character('b'), Character('c')),
      Disjunction(Character('b'), Character('c'), Character('d')));
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

TEST(Normalised, AnyCharacter) {
  EXPECT_NORMALISED(
      AnyCharacter(),
      AnyCharacter());
}

TEST(Normalised, Character) {
  EXPECT_NORMALISED(
      Character('a'),
      Character('a'));
}

TEST(Normalised, CharacterClass) {
  EXPECT_NORMALISED(
      CharacterClass({'a', 'b', 'c'}),
      CharacterClass({'a', 'b', 'c'}));
}

TEST(Normalised, KleeneClosure) {
  EXPECT_NORMALISED(
      KleeneClosure(Character('a')),
      KleeneClosure(KleeneClosure(Character('a'))));
  EXPECT_NORMALISED(
      EmptyString(),
      KleeneClosure(EmptySet()));
  EXPECT_NORMALISED(
      EmptyString(),
      KleeneClosure(EmptyString()));
  EXPECT_NORMALISED(
      Complement(EmptySet()),
      KleeneClosure(AnyCharacter()));
}

TEST(Normalised, Concatenation) {
  EXPECT_NORMALISED(
      Concatenation(
          Character('a'),
          Concatenation(
              Character('b'),
              Character('c'))),
      Concatenation(
          Concatenation(
              Character('a'),
              Character('b')),
          Character('c')));
  EXPECT_NORMALISED(
      EmptySet(),
      Concatenation(EmptySet(), Character('a')));
  EXPECT_NORMALISED(
      EmptySet(),
      Concatenation(Character('a'), EmptySet()));
  EXPECT_NORMALISED(
      Character('a'),
      Concatenation(EmptyString(), Character('a')));
  EXPECT_NORMALISED(
      Character('a'),
      Concatenation(Character('a'), EmptyString()));
}

TEST(Normalised, Complement) {
  EXPECT_NORMALISED(
      Character('a'),
      Complement(Complement(Character('a'))));
}

TEST(Normalised, Conjunction) {
  EXPECT_NORMALISED(
      Conjunction(
          Character('a'),
          Character('b'),
          Character('c')),
      Conjunction(
          Conjunction(
              Character('a'),
              Character('b')),
          Character('c')));
  EXPECT_NORMALISED(
      Conjunction(Character('a'), Character('b')),
      Conjunction(Character('b'), Character('a')));
  EXPECT_NORMALISED(
      Character('a'),
      Conjunction(Character('a'), Character('a')));
  EXPECT_NORMALISED(
      EmptySet(),
      Conjunction(Character('a'), EmptySet()));
  EXPECT_NORMALISED(
      Character('a'),
      Conjunction(Character('a'), Complement(EmptySet())));
}

TEST(Normalised, Disjunction) {
  EXPECT_NORMALISED(
      Disjunction(
          Character('a'),
          Character('b'),
          Character('c')),
      Disjunction(
          Disjunction(
              Character('a'),
              Character('b')),
          Character('c')));
  EXPECT_NORMALISED(
      Disjunction(Character('a'), Character('b')),
      Disjunction(Character('b'), Character('a')));
  EXPECT_NORMALISED(
      Character('a'),
      Disjunction(Character('a'), Character('a')));
  EXPECT_NORMALISED(
      Character('a'),
      Disjunction(Character('a'), EmptySet()));
  EXPECT_NORMALISED(
      Complement(EmptySet()),
      Disjunction(Character('a'), Complement(EmptySet())));
}

#define EXPECT_NULLABILITY(expected, exp)              \
  do {                                                 \
    EXPECT_EQ(expected, Normalised(Nullability(exp))); \
  } while (0)

TEST(Nullability, EmptySet) {
  EXPECT_NULLABILITY(
      EmptySet(),
      EmptySet());
}

TEST(Nullability, EmptyString) {
  EXPECT_NULLABILITY(
      EmptyString(),
      EmptyString());
}

TEST(Nullability, AnyCharacter) {
  EXPECT_NULLABILITY(
      EmptySet(),
      AnyCharacter());
}

TEST(Nullability, Character) {
  EXPECT_NULLABILITY(
      EmptySet(),
      Character('a'));
}

TEST(Nullability, CharacterClass) {
  EXPECT_NULLABILITY(
      EmptySet(),
      CharacterClass({'a', 'b', 'c'}));
}

TEST(Nullability, KleeneClosure) {
  EXPECT_NULLABILITY(
      EmptyString(),
      KleeneClosure(Character('a')));
}

TEST(Nullability, Concatenation) {
  EXPECT_NULLABILITY(
      EmptySet(),
      Concatenation(Character('a'), Character('b')));
}

TEST(Nullability, Complement) {
  EXPECT_NULLABILITY(
      EmptyString(),
      Complement(Character('a')));
}

TEST(Nullability, Conjunction) {
  EXPECT_NULLABILITY(
      EmptySet(),
      Conjunction(Character('a'), Character('b')));
}

TEST(Nullability, Disjunction) {
  EXPECT_NULLABILITY(
      EmptySet(),
      Disjunction(Character('a'), Character('b')));
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

TEST(Derivative, AnyCharacter) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      AnyCharacter());
}

TEST(Derivative, Character) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      Character('a'));
  EXPECT_DERIVATIVE(
      EmptySet(),
      Character('b'));
}

TEST(Derivative, CharacterClass) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      CharacterClass({'a', 'b', 'c'}));
  EXPECT_DERIVATIVE(
      EmptySet(),
      CharacterClass({'b', 'c', 'd'}));
}

TEST(Derivative, KleeneClosure) {
  EXPECT_DERIVATIVE(
      KleeneClosure(Character('a')),
      KleeneClosure(Character('a')));
}

TEST(Derivative, Concatenation) {
  EXPECT_DERIVATIVE(
      Character('b'),
      Concatenation(Character('a'), Character('b')));
  EXPECT_DERIVATIVE(
      Concatenation(KleeneClosure(Character('a')), Character('b')),
      Concatenation(KleeneClosure(Character('a')), Character('b')));
}

TEST(Derivative, Complement) {
  EXPECT_DERIVATIVE(
      Complement(EmptyString()),
      Complement(Character('a')));
}

TEST(Derivative, Conjunction) {
  EXPECT_DERIVATIVE(
      EmptySet(),
      Conjunction(Character('a'), Character('b')));
}

TEST(Derivative, Disjunction) {
  EXPECT_DERIVATIVE(
      EmptyString(),
      Disjunction(Character('a'), Character('b')));
}

#define EXPECT_PARTITIONS(expected, exp) \
  do {                                   \
    list<set<Rune>> partitions;          \
    Partitions(exp, &partitions);        \
    EXPECT_EQ(expected, partitions);     \
  } while (0)

TEST(Partitions, EmptySet) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({})}),
      EmptySet());
}

TEST(Partitions, EmptyString) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({})}),
      EmptyString());
}

TEST(Partitions, AnyCharacter) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({})}),
      AnyCharacter());
}

TEST(Partitions, Character) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a'}),
                       set<Rune>({'a'})}),
      Character('a'));
}

TEST(Partitions, CharacterClass) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a', 'b', 'c'}),
                       set<Rune>({'a', 'b', 'c'})}),
      CharacterClass({'a', 'b', 'c'}));
}

TEST(Partitions, KleeneClosure) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a'}),
                       set<Rune>({'a'})}),
      KleeneClosure(Character('a')));
}

TEST(Partitions, Concatenation) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a'}),
                       set<Rune>({'a'})}),
      Concatenation(Character('a'), Character('b')));
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a', 'b'}),
                       set<Rune>({'b'}),
                       set<Rune>({'a'})}),
      Concatenation(KleeneClosure(Character('a')), Character('b')));
}

TEST(Partitions, Complement) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a'}),
                       set<Rune>({'a'})}),
      Complement(Character('a')));
}

TEST(Partitions, Conjunction) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a', 'b'}),
                       set<Rune>({'b'}),
                       set<Rune>({'a'})}),
      Conjunction(Character('a'), Character('b')));
}

TEST(Partitions, Disjunction) {
  EXPECT_PARTITIONS(
      list<set<Rune>>({set<Rune>({'a', 'b'}),
                       set<Rune>({'b'}),
                       set<Rune>({'a'})}),
      Disjunction(Character('a'), Character('b')));
}

#define EXPECT_PARSE(expected, str) \
  do {                              \
    Exp exp;                        \
    ASSERT_TRUE(Parse(str, &exp));  \
    EXPECT_EQ(expected, exp);       \
  } while (0)

TEST(Parse, AnyCharacter) {
  EXPECT_PARSE(
      AnyCharacter(),
      ".");
}

TEST(Parse, Character) {
  EXPECT_PARSE(
      Character('a'),
      "a");
}

TEST(Parse, CharacterClass) {
  EXPECT_PARSE(
      CharacterClass({'a', 'b', 'c'}),
      "[abc]");
  EXPECT_PARSE(
      Conjunction(
          Complement(
              CharacterClass({'a', 'b', 'c'})),
          AnyCharacter()),
      "[^abc]");
}

TEST(Parse, KleeneClosure) {
  EXPECT_PARSE(
      KleeneClosure(
          Character('a')),
      "a*");
  EXPECT_PARSE(
      KleeneClosure(
          KleeneClosure(
              Character('a'))),
      "a**");
  EXPECT_PARSE(
      Concatenation(
          Character('a'),
          KleeneClosure(
              Character('b'))),
      "ab*");
  EXPECT_PARSE(
      KleeneClosure(
          Concatenation(
              Character('a'),
              Character('b'))),
      "(ab)*");
  EXPECT_PARSE(
      Concatenation(
          KleeneClosure(
              Character('a')),
          Character('b')),
      "a*b");
  EXPECT_PARSE(
      Concatenation(
          KleeneClosure(
              Character('a')),
          Concatenation(
              KleeneClosure(
                  Character('b')),
              Character('c'))),
      "a*b*c");
}

TEST(Parse, Concatenation) {
  EXPECT_PARSE(
      Concatenation(
          Character('a'),
          Character('b')),
      "ab");
  EXPECT_PARSE(
      Concatenation(
          Character('a'),
          Concatenation(
              Character('b'),
              Character('c'))),
      "abc");
}

TEST(Parse, Complement) {
  EXPECT_PARSE(
      Complement(
          Character('a')),
      "!a");
  EXPECT_PARSE(
      Complement(
          Complement(
              Character('a'))),
      "!!a");
  EXPECT_PARSE(
      Complement(
          Concatenation(
              Character('a'),
              Character('b'))),
      "!ab");
  EXPECT_PARSE(
      Complement(
          Concatenation(
              Character('a'),
              Character('b'))),
      "!(ab)");
  EXPECT_PARSE(
      Concatenation(
          Character('a'),
          Complement(
              Character('b'))),
      "a!b");
  EXPECT_PARSE(
      Concatenation(
          Concatenation(
              Character('a'),
              Complement(
                  Character('b'))),
          Complement(
              Character('c'))),
      "a!b!c");
}

TEST(Parse, Conjunction) {
  EXPECT_PARSE(
      Conjunction(
          Character('a'),
          Character('b')),
      "a&b");
  EXPECT_PARSE(
      Conjunction(
          Conjunction(
              Character('a'),
              Character('b')),
          Character('c')),
      "a&b&c");
}

TEST(Parse, Disjunction) {
  EXPECT_PARSE(
      Disjunction(
          Character('a'),
          Character('b')),
      "a|b");
  EXPECT_PARSE(
      Disjunction(
          Disjunction(
              Character('a'),
              Character('b')),
          Character('c')),
      "a|b|c");
}

#define EXPECT_MATCH(expected, str)  \
  do {                               \
    if (expected) {                  \
      EXPECT_TRUE(Match(exp, str));  \
      EXPECT_TRUE(Match(dfa, str));  \
    } else {                         \
      EXPECT_FALSE(Match(exp, str)); \
      EXPECT_FALSE(Match(dfa, str)); \
    }                                \
  } while (0)

TEST(Match, EmptySet) {
  Exp exp;
  DFA dfa;
  exp = EmptySet();
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(false, "a");
}

TEST(Match, EmptyString) {
  Exp exp;
  DFA dfa;
  exp = EmptyString();
  Compile(exp, &dfa);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(false, "a");
}

TEST(Match, AnyCharacter) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse(".", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
}

TEST(Match, Character) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("a", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(false, "X");
}

TEST(Match, CharacterClass) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("[abc]", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(false, "X");
  ASSERT_TRUE(Parse("[^abc]", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(false, "a");
  EXPECT_MATCH(true, "X");
}

TEST(Match, KleeneClosure) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("a*", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(true, "aa");
  ASSERT_TRUE(Parse("a+", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(true, "aa");
  ASSERT_TRUE(Parse("a?", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(true, "a");
  EXPECT_MATCH(false, "aa");
}

TEST(Match, Concatenation) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("aa", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "");
  EXPECT_MATCH(false, "a");
  EXPECT_MATCH(true, "aa");
  EXPECT_MATCH(false, "aaa");
}

TEST(Match, Complement) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("!a", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(true, "");
  EXPECT_MATCH(false, "a");
  EXPECT_MATCH(true, "aa");
}

TEST(Match, Conjunction) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("a.&.b", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(false, "aa");
  EXPECT_MATCH(true, "ab");
  EXPECT_MATCH(false, "ba");
  EXPECT_MATCH(false, "bb");
}

TEST(Match, Disjunction) {
  Exp exp;
  DFA dfa;
  ASSERT_TRUE(Parse("a.|.b", &exp));
  Compile(exp, &dfa);
  EXPECT_MATCH(true, "aa");
  EXPECT_MATCH(true, "ab");
  EXPECT_MATCH(false, "ba");
  EXPECT_MATCH(true, "bb");
}

}  // namespace redgrep
