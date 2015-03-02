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

%code requires {

#include "llvm/ADT/StringRef.h"
#include "regexp.h"
#include "utf.h"

namespace redgrep_yy {
class location;
}  // namespace redgrep_yy

#define YYSTYPE redgrep::Exp
#define YYLTYPE redgrep_yy::location

namespace redgrep_yy {

struct Data {
  Data(llvm::StringRef str, redgrep::Exp* exp) : str_(str), exp_(exp) {}
  ~Data() {}

  llvm::StringRef str_;
  redgrep::Exp* exp_;
};

}  // namespace redgrep_yy

#define YYDATA redgrep_yy::Data

int yylex(YYSTYPE* lvalp, YYLTYPE* llocp, YYDATA* yydata);

}

%lex-param   { YYDATA* yydata }
%parse-param { YYDATA* yydata }

%left DISJUNCTION
%left CONJUNCTION
%left COMPLEMENT
%right CONCATENATION
%left QUANTIFIER
%nonassoc LEFT_PARENTHESIS RIGHT_PARENTHESIS
%nonassoc FUNDAMENTAL
%token ERROR

%%

start:
  expression
  { *yydata->exp_ = $1; }

expression:
  expression DISJUNCTION expression
  { $$ = redgrep::Disjunction($1, $3); }
| expression CONJUNCTION expression
  { $$ = redgrep::Conjunction($1, $3); }
| COMPLEMENT expression
  { $$ = redgrep::Complement($2); }
| expression expression %prec CONCATENATION
  { $$ = redgrep::Concatenation($1, $2); }
| expression QUANTIFIER
  { redgrep::Exp sub; int min; int max;
    std::tie(sub, min, max) = $2->quantifier();
    redgrep::Mode mode; bool capture;
    std::tie(std::ignore, std::ignore, mode, capture) = sub->group();
    $$ = redgrep::Quantifier($1, min, max);
    $$ = redgrep::Group(-1, $$, mode, capture); }
| LEFT_PARENTHESIS expression RIGHT_PARENTHESIS
  { redgrep::Mode mode; bool capture;
    std::tie(std::ignore, std::ignore, mode, capture) = $1->group();
    $$ = redgrep::Group(-1, $2, mode, capture); }
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
    *input = input->drop_front(len);
    return true;
  }
  return false;
}

static bool CharacterClass(llvm::StringRef* input,
                           std::set<Rune>* characters,
                           bool* complement) {
  if (input->startswith("^")) {
    *input = input->drop_front(1);
    *complement = true;
  } else {
    *complement = false;
  }
  Rune character;
  while (Character(input, &character)) {
    switch (character) {
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
        characters->insert(character);
        break;
      case ']':
        return true;
    }
  }
  return false;
}

static bool Quantifier(Rune character,
                       llvm::StringRef* input,
                       int* min,
                       int* max) {
  static const char kDigits[] = "0123456789";
  auto Number = [&input](int* output) -> bool {
    if (input->find_first_of(kDigits) == 0) {
      size_t len = input->find_first_not_of(kDigits);
      if (len != llvm::StringRef::npos && len <= 3) {
        sscanf(input->data(), "%d", output);
        *input = input->drop_front(len);
        return true;
      }
    }
    return false;
  };
  switch (character) {
    case '*':
      *min = 0;
      *max = -1;
      return true;
    case '+':
      *min = 1;
      *max = -1;
      return true;
    case '?':
      *min = 0;
      *max = 1;
      return true;
    case '{': {
      if (Number(min) && *min >= 0) {
        if (input->startswith("}")) {  // {n}
          *input = input->drop_front(1);
          *max = *min;
          return true;
        }
        if (input->startswith(",")) {
          *input = input->drop_front(1);
          if (input->startswith("}")) {  // {n,}
            *input = input->drop_front(1);
            *max = -1;
            return true;
          }
          if (Number(max) && *max >= *min) {
            if (input->startswith("}")) {  // {n,m}
              *input = input->drop_front(1);
              return true;
            }
          }
        }
      }
      return false;
    }
    default:
      break;
  }
  abort();
}

}  // namespace

typedef redgrep_yy::parser::token_type TokenType;

int yylex(YYSTYPE* lvalp, YYLTYPE* llocp, YYDATA* yydata) {
  Rune character;
  if (!Character(&yydata->str_, &character)) {
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
    case '+':
    case '?':
    case '{': {
      int min, max;
      if (!Quantifier(character, &yydata->str_, &min, &max)) {
        return TokenType::ERROR;
      }
      redgrep::Mode mode;
      bool capture = false;
      if (yydata->str_.startswith("?")) {
        yydata->str_ = yydata->str_.drop_front(1);
        mode = redgrep::kMinimal;
      } else {
        mode = redgrep::kMaximal;
      }
      // Somewhat perversely, we bundle the Group into the Quantifier and then
      // rebundle them back in the parser action.
      *lvalp = redgrep::Group(-1, redgrep::Byte(-1), mode, capture);
      *lvalp = redgrep::Quantifier(*lvalp, min, max);
      return TokenType::QUANTIFIER;
    }
    case '(': {
      redgrep::Mode mode = redgrep::kPassive;
      bool capture;
      if (yydata->str_.startswith("?:")) {
        yydata->str_ = yydata->str_.drop_front(2);
        capture = false;
      } else {
        capture = true;
      }
      *lvalp = redgrep::Group(-1, redgrep::Byte(-1), mode, capture);
      return TokenType::LEFT_PARENTHESIS;
    }
    case ')':
      return TokenType::RIGHT_PARENTHESIS;
    case '[': {
      std::set<Rune> characters;
      bool complement;
      if (!CharacterClass(&yydata->str_, &characters, &complement) ||
          characters.empty()) {
        return TokenType::ERROR;
      }
      *lvalp = redgrep::CharacterClass(characters, complement);
      return TokenType::FUNDAMENTAL;
    }
    case '\\':
      if (!Character(&yydata->str_, &character)) {
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
