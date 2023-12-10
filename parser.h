#pragma once

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
};
extern int curTok;
int getNextToken();

std::unique_ptr<FunctionAST> parseDefinition();
std::unique_ptr<PrototypeAST> parseExtern();
std::unique_ptr<FunctionAST> parseDefinition();
std::unique_ptr<FunctionAST> parseTopLevelExpr();