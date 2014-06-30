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
#include "utf/utf.h"

namespace redgrep_yy {
class location;
}  // namespace redgrep_yy

#define YYSTYPE redgrep::Exp
#define YYLTYPE redgrep_yy::location

namespace redgrep_yy {

class Data {
 public:
  Data(llvm::StringRef str, redgrep::Exp* exp)
      : str_(str),
        exp_(exp),
        modes_(nullptr),
        groups_(nullptr),
        stack_() {
  }

  Data(llvm::StringRef str, redgrep::Exp* exp,
       std::vector<int>* modes, std::vector<int>* groups)
      : str_(str),
        exp_(exp),
        modes_(modes),
        groups_(groups),
        stack_() {
    modes_->clear();
    groups_->clear();
  }

  virtual ~Data() {}

  // Numbers exp, which must be a Tag expression.
  // Updates modes in order to record the mode of exp.
  // Updates groups iff exp is opening a capturing group.
  redgrep::Exp Number(redgrep::Exp exp) {
    int num = exp->tag().first;
    int mode = exp->tag().second;
    if (num <= 0) {
      int left = modes_->size();
      modes_->push_back(0);
      int right = modes_->size();
      modes_->push_back(0);
      if (num == -1) {
        groups_->push_back(left);
        groups_->push_back(right);
      }
      num = left;
      stack_.push_front(right);
    } else {
      num = stack_.front();
      stack_.pop_front();
    }
    (*modes_)[num] = mode;
    return redgrep::Tag(num, mode);
  }

  llvm::StringRef str_;
  redgrep::Exp* exp_;

 private:
  std::vector<int>* modes_;   // Not owned.
  std::vector<int>* groups_;  // Not owned.
  std::list<int> stack_;

  //DISALLOW_COPY_AND_ASSIGN(Data);
  Data(const Data&) = delete;
  void operator=(const Data&) = delete;
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
  { // This left parenthesis (non-capturing) is minimal.
    redgrep::Exp left = redgrep::Tag(0, -1);
    // This right parenthesis is maximal.
    redgrep::Exp right = redgrep::Tag(1, 1);
    $$ = redgrep::Complement($2);
    $$ = redgrep::Concatenation(left, $$, right); }
| expression expression %prec CONCATENATION
  { $$ = redgrep::Concatenation($1, $2); }
| expression QUANTIFIER
  { // This left parenthesis (non-capturing) is minimal.
    redgrep::Exp left = redgrep::Tag(0, -1);
    redgrep::Exp right; int min; int max;
    std::tie(right, min, max) = $2->quantifier();
    $$ = redgrep::Quantifier($1, min, max);
    $$ = redgrep::Concatenation(left, $$, right); }
| LEFT_PARENTHESIS expression RIGHT_PARENTHESIS
  { $$ = redgrep::Concatenation($1, $2, $3); }
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
      if (yydata->str_.startswith("?")) {
        yydata->str_ = yydata->str_.drop_front(1);
        // This right parenthesis is minimal.
        *lvalp = redgrep::Tag(1, -1);
      } else {
        // This right parenthesis is maximal.
        *lvalp = redgrep::Tag(1, 1);
      }
      // Somewhat perversely, we bundle the right parenthesis with min and max
      // and unbundle them back in the parser action.
      *lvalp = redgrep::Quantifier(*lvalp, min, max);
      return TokenType::QUANTIFIER;
    }
    case '(':
      if (yydata->str_.startswith("?:")) {
        yydata->str_ = yydata->str_.drop_front(2);
        // This left parenthesis (non-capturing) is passive.
        *lvalp = redgrep::Tag(0, 0);
      } else {
        // This left parenthesis (capturing) is passive.
        *lvalp = redgrep::Tag(-1, 0);
      }
      return TokenType::LEFT_PARENTHESIS;
    case ')':
      // This right parenthesis is passive.
      *lvalp = redgrep::Tag(1, 0);
      return TokenType::RIGHT_PARENTHESIS;
    case '[': {
      std::set<Rune> character_class;
      bool complement;
      if (!CharacterClass(&yydata->str_, &character_class, &complement) ||
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
