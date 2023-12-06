#include "ast.h"

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "llvm/IR/Function.h"

using namespace std;

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
};

static std::string identifierStr; // Filled in if tok_identifier
static double numVal;             // Filled in if tok_number

static int getTok() {
  static int lastChar = ' ';
  while (isspace(lastChar)) {
    lastChar = getchar();
  }
  if (isalpha(lastChar)) {
    identifierStr = lastChar;
    while (isalnum((lastChar = getchar())))
      identifierStr += lastChar;
    if (identifierStr == "def")
      return tok_def;
    if (identifierStr == "extern")
      return tok_extern;
    return tok_identifier;
  } else if (isdigit(lastChar) || lastChar == '.') {
    std::string numStr;
    do {
      numStr += lastChar;
      lastChar = getchar();
    } while (isdigit(lastChar) || lastChar == '.');
    numVal = strtod(numStr.c_str(), 0);
    return tok_number;
  } else if (lastChar == '#') {
    do
      lastChar = getchar();
    while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');
    if (lastChar != EOF) {
      return getTok();
    }
  }
  if (lastChar == EOF) {
    return tok_eof;
  } else {
    int thisChar = lastChar;
    lastChar = getchar();
    return thisChar;
  }
}

static int curTok = 0;
static int getNextToken() { return curTok = getTok(); }

static void LogDebug(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, fmt, args);
  va_end(args);
}

static void LogInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, fmt, args);
  va_end(args);
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *str) {
  LogError(str);
  return nullptr;
}

static std::unique_ptr<ExprAST> parseExpression();

static std::unique_ptr<ExprAST> parseNumberExpr() {
  auto result = std::make_unique<NumberExprAST>(numVal);
  getNextToken();
  LogDebug("parseNumberExpr: %f\n", numVal);
  return std::move(result);
}

static std::unique_ptr<ExprAST> parseParenExpr() {
  getNextToken(); // eat (
  auto v = parseExpression();
  if (!v) {
    return nullptr;
  }
  if (curTok != ')') {
    return LogError("expected ')'");
  }
  getNextToken(); // eat )
  LogDebug("parseParenExpr\n");
  return v;
}

static std::unique_ptr<ExprAST> parseIdentifierExpr() {
  std::string idName = identifierStr;
  getNextToken();      // eat identifier
  if (curTok != '(') { // simple variable reference
    LogDebug("parseIdentifierExpr: %s\n", idName.c_str());
    return std::make_unique<VariableExprAST>(idName);
  }
  // call expression
  getNextToken();
  std::vector<std::unique_ptr<ExprAST>> args;
  if (curTok != ')') {
    while (1) {
      if (auto arg = parseExpression())
        args.push_back(std::move(arg));
      else
        return nullptr;
      if (curTok == ')')
        break;
      if (curTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }
  getNextToken(); // eat )
  LogDebug("parseIdentifierExpr: call %s(%d args)\n", idName.c_str(),
           args.size());
  return std::make_unique<CallExprAST>(idName, std::move(args));
}

static std::unique_ptr<ExprAST> parsePrimary() {
  switch (curTok) {
  case tok_identifier:
    return parseIdentifierExpr();
  case tok_number:
    return parseNumberExpr();
  case '(':
    return parseParenExpr();
  default:
    return LogError("unknown token when expecting an expresssion");
  }
}

// binary expression

static const std::map<char, int> binOpPrecedence = {
    // 1 is lowest precedence
    {'<', 10},
    {'+', 20},
    {'-', 20},
    {'*', 40}};

static int getTokPrecedence() {
  if (!isascii(curTok))
    return -1;
  auto it = binOpPrecedence.find(curTok);
  return it == binOpPrecedence.end() ? -1 : it->second;
}

static std::unique_ptr<ExprAST> parseBinOpRhs(int exprPrec,
                                              std::unique_ptr<ExprAST> lhs) {
  while (true) {
    int tokPrec = getTokPrecedence();
    if (tokPrec < exprPrec)
      return lhs;

    int binOp = curTok;
    getNextToken();
    // now we know this is binary operation
    auto rhs = parsePrimary();
    if (!rhs)
      return nullptr;
    int nextPrec = getTokPrecedence();
    LogDebug("parseBinOpRhs: op %c(%d) prec %d\n", binOp, binOp, tokPrec);
    if (tokPrec < nextPrec) {
      rhs = parseBinOpRhs(tokPrec + 1, std::move(rhs));
      if (!rhs)
        return nullptr;
    }
    lhs =
        std::make_unique<BinaryExprAST>(binOp, std::move(lhs), std::move(rhs));
  }
}

static std::unique_ptr<ExprAST> parseExpression() {
  LogDebug("parseExpression\n");
  auto lhs = parsePrimary();
  if (!lhs)
    return nullptr;
  return parseBinOpRhs(0, std::move(lhs));
}

static std::unique_ptr<PrototypeAST> parsePrototype() {
  if (curTok != tok_identifier)
    return LogErrorP("expected function name in prototype");

  std::string fnName = identifierStr;
  getNextToken();
  if (curTok != '(')
    return LogErrorP("expected '(' in prototype");
  std::vector<std::string> argNames;
  while (getNextToken() == tok_identifier)
    argNames.push_back(identifierStr);

  if (curTok != ')')
    return LogErrorP("expected ')' in prototype");
  getNextToken(); // eat )
  LogDebug("parsePrototype %s(%d args)\n", fnName.c_str(), argNames.size());
  return std::make_unique<PrototypeAST>(fnName, std::move(argNames));
}

static std::unique_ptr<FunctionAST> parseDefinition() {
  getNextToken();
  auto proto = parsePrototype();
  if (!proto)
    return nullptr;

  auto expr = parseExpression();
  if (!expr)
    return nullptr;
  LogDebug("parseDefinition %s\n", proto->getName().c_str());
  return std::make_unique<FunctionAST>(std::move(proto), std::move(expr));
}

static std::unique_ptr<PrototypeAST> parseExtern() {
  getNextToken();
  return parsePrototype();
}

static std::unique_ptr<FunctionAST> parseTopLevelExpr() {
  auto expr = parseExpression();
  if (!expr)
    return nullptr;
  auto proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
  return std::make_unique<FunctionAST>(std::move(proto), std::move(expr));
}

static void handleDefinition() {
  if (auto FuncAST = parseDefinition()) {
    if (auto *FuncIR = FuncAST->codegen()) {
      LogInfo("function definition:\n");
      FuncIR->print(errs());
      LogInfo("\n");
    }
  } else {
    getNextToken(); // skip next token
  }
}

static void handleExtern() {
  if (auto ProtoAST = parsePrototype()) {
    if (auto *FuncIR = ProtoAST->codegen()) {
      LogInfo("extern function:\n");
      FuncIR->print(errs());
      LogInfo("\n");
    }
  } else {
    getNextToken(); // skip next token
  }
}

static void handleTopLevelExpr() {
  if (auto FuncAST = parseTopLevelExpr()) {
    if (auto *FuncIR = FuncAST->codegen()) {
      LogInfo("top level expression:\n");
      FuncIR->print(errs());
      LogInfo("\n");
      // remove anonymous expression
      FuncIR->eraseFromParent();
    } else {
      getNextToken(); // skip next token
    }
  }
}

static void mainLoop() {
  fprintf(stdout, "kal> ");
  getNextToken();
  while (true) {
    fprintf(stdout, "kal> ");
    switch (curTok) {
    case tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case tok_def:
      handleDefinition();
      break;
    case tok_extern:
      handleExtern();
      break;
    default:
      handleTopLevelExpr();
      break;
    }
  }
}

int main(void) {
  initModule();
  mainLoop();
  return 0;
}