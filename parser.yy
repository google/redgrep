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

%skeleton "lalr1.cc"
%name-prefix "redgrep_yy"
%defines
%locations

%{

#include "llvm/ADT/StringRef.h"
#include "regexp.h"
#include "utf/utf.h"

namespace redgrep_yy {
class location;
}  // namespace redgrep_yy

#define YYSTYPE redgrep::Exp
#define YYLTYPE redgrep_yy::location

int yylex(YYSTYPE* lvalp, YYLTYPE* llocp, llvm::StringRef* input);

%}

%lex-param { llvm::StringRef* input }

%parse-param { llvm::StringRef* input }
%parse-param { redgrep::Exp* output }

%left DISJUNCTION
%left CONJUNCTION
%left COMPLEMENT
%right CONCATENATION
%left ZERO_OR_MORE ONE_OR_MORE ZERO_OR_ONE
%nonassoc LEFT_PARENTHESIS RIGHT_PARENTHESIS
%nonassoc FUNDAMENTAL
%token ERROR

%%

start:
  expression
  { *output = $1; }

expression:
  expression DISJUNCTION expression
  { $$ = redgrep::Disjunction($1, $3); }
| expression CONJUNCTION expression
  { $$ = redgrep::Conjunction($1, $3); }
| COMPLEMENT expression
  { $$ = redgrep::Complement($2); }
| expression expression %prec CONCATENATION
  { $$ = redgrep::Concatenation($1, $2); }
| expression ZERO_OR_MORE
  { $$ = redgrep::KleeneClosure($1); }
| expression ONE_OR_MORE
  { $$ = redgrep::Concatenation($1, redgrep::KleeneClosure($1)); }
| expression ZERO_OR_ONE
  { $$ = redgrep::Disjunction(redgrep::EmptyString(), $1); }
| LEFT_PARENTHESIS expression RIGHT_PARENTHESIS
  { $$ = $2; }
| FUNDAMENTAL
  { $$ = $1; }

%%

namespace redgrep_yy {

void parser::error(const YYLTYPE&, const std::string&) {
  // TODO(junyer): Do something?
}

}  // namespace redgrep_yy

namespace {

static bool Character(llvm::StringRef* input,
                      Rune* character) {
  int len = charntorune(character, input->data(), input->size());
  if (len > 0) {
    *input = input->substr(len);
    return true;
  }
  return false;
}

static bool CharacterClass(llvm::StringRef* input,
                           std::set<Rune>* character_class,
                           bool* complement) {
  character_class->clear();
  *complement = false;
  Rune character;
  for (int i = 0; Character(input, &character); ++i) {
    switch (character) {
      case '^':
        if (i == 0) {
          *complement = true;
        } else {
          character_class->insert(character);
        }
        break;
      case '\\':
        if (!Character(input, &character)) {
          return false;
        }
        switch (character) {
          case 'f':
            character = '\f';
            break;
          case 'n':
            character = '\n';
            break;
          case 'r':
            character = '\r';
            break;
          case 't':
            character = '\t';
            break;
          default:
            break;
        }
        // FALLTHROUGH
      default:
        character_class->insert(character);
        break;
      case ']':
        return true;
    }
  }
  return false;
}

}  // namespace

typedef redgrep_yy::parser::token_type TokenType;

int yylex(YYSTYPE* lvalp, YYLTYPE* llocp, llvm::StringRef* input) {
  Rune character;
  if (!Character(input, &character)) {
    return 0;
  }
  switch (character) {
    case '|':
      return TokenType::DISJUNCTION;
    case '&':
      return TokenType::CONJUNCTION;
    case '!':
      return TokenType::COMPLEMENT;
    case '*':
      return TokenType::ZERO_OR_MORE;
    case '+':
      return TokenType::ONE_OR_MORE;
    case '?':
      return TokenType::ZERO_OR_ONE;
    case '(':
      return TokenType::LEFT_PARENTHESIS;
    case ')':
      return TokenType::RIGHT_PARENTHESIS;
    case '[': {
      std::set<Rune> character_class;
      bool complement;
      if (!CharacterClass(input, &character_class, &complement) ||
          character_class.empty()) {
        return TokenType::ERROR;
      }
      *lvalp = redgrep::CharacterClass(character_class);
      if (complement) {
        *lvalp = redgrep::Conjunction(redgrep::Complement(*lvalp),
                                      redgrep::AnyCharacter());
      }
      return TokenType::FUNDAMENTAL;
    }
    case '\\':
      if (!Character(input, &character)) {
        return TokenType::ERROR;
      }
      switch (character) {
        case 'C':
          *lvalp = redgrep::AnyByte();
          return TokenType::FUNDAMENTAL;
        case 'f':
          character = '\f';
          break;
        case 'n':
          character = '\n';
          break;
        case 'r':
          character = '\r';
          break;
        case 't':
          character = '\t';
          break;
        default:
          break;
      }
      // FALLTHROUGH
    default:
      *lvalp = redgrep::Character(character);
      return TokenType::FUNDAMENTAL;
    case '.':
      *lvalp = redgrep::AnyCharacter();
      return TokenType::FUNDAMENTAL;
  }
}
