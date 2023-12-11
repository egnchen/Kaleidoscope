#pragma once
#include <map>

#include "ast.h"

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,

  // control flow stuff
  tok_if = -6,
  tok_then = -7,
  tok_else = -8,

  tok_for = -9,
  tok_in = -10,

  // user-defined operators
  tok_unary = -11,
  tok_binary = -12,

  // variables
  tok_var = -13,
};
extern int curTok;
int getNextToken();

extern std::map<char, int> binOpPrecedence;

std::unique_ptr<FunctionAST> parseDefinition();
std::unique_ptr<PrototypeAST> parseExtern();
std::unique_ptr<FunctionAST> parseDefinition();
std::unique_ptr<FunctionAST> parseTopLevelExpr();