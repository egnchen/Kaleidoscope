#include "parser.h"
#include "ast.h"
#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/AtomicOrdering.h"

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
    if (identifierStr == "if")
      return tok_if;
    if (identifierStr == "then")
      return tok_then;
    if (identifierStr == "else")
      return tok_else;
    if (identifierStr == "for")
      return tok_for;
    if (identifierStr == "in")
      return tok_in;
    if (identifierStr == "unary")
      return tok_unary;
    if (identifierStr == "binary")
      return tok_binary;
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

int curTok = 0;
int getNextToken() { return curTok = getTok(); }

std::unique_ptr<PrototypeAST> LogErrorP(const char *str) {
  LogError(str);
  return nullptr;
}

static std::unique_ptr<ExprAST> parseExpr();

static std::unique_ptr<ExprAST> parseNumberExpr() {
  auto result = std::make_unique<NumberExprAST>(numVal);
  getNextToken();
  LogDebug("parseNumberExpr: %f\n", numVal);
  return std::move(result);
}

static std::unique_ptr<ExprAST> parseParenExpr() {
  getNextToken(); // eat (
  auto v = parseExpr();
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
      if (auto arg = parseExpr())
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

static std::unique_ptr<ExprAST> parseIfExpr() {
  getNextToken();
  auto cond = parseExpr();
  if (!cond)
    return nullptr;

  if (curTok != tok_then)
    return LogError("expected then");
  getNextToken();
  auto then_ = parseExpr();
  if (!then_)
    return nullptr;

  if (curTok != tok_else)
    return LogError("expected else");
  getNextToken();
  auto else_ = parseExpr();
  if (!else_)
    return nullptr;

  LogDebug("parseIfExpr\n");
  return std::make_unique<IfExprAST>(std::move(cond), std::move(then_),
                                     std::move(else_));
}

static std::unique_ptr<ExprAST> parseForExpr() {
  getNextToken(); // eat for

  if (curTok != tok_identifier)
    return LogError("expected identifier after for");
  std::string varName = identifierStr;
  getNextToken();

  if (curTok != '=')
    return LogError("exptected '=' after loop var");
  getNextToken();

  auto start = parseExpr();
  if (!start)
    return nullptr;
  if (curTok != ',')
    return LogError("expected ',' after start value");
  getNextToken();

  auto end = parseExpr();
  if (!end)
    return nullptr;

  // optional step value
  std::unique_ptr<ExprAST> step;
  if (curTok == ',') {
    getNextToken();
    step = parseExpr();
    if (!step)
      return nullptr;
  }

  if (curTok != tok_in)
    return LogError("expected 'in' after for");
  getNextToken();

  auto body = parseExpr();
  if (!body)
    return nullptr;

  return std::make_unique<ForExprAST>(varName, std::move(start), std::move(end),
                                      std::move(step), std::move(body));
}

static std::unique_ptr<ExprAST> parsePrimary() {
  switch (curTok) {
  case tok_identifier:
    return parseIdentifierExpr();
  case tok_number:
    return parseNumberExpr();
  case '(':
    return parseParenExpr();
  case tok_if:
    return parseIfExpr();
  case tok_for:
    return parseForExpr();
  default:
    return LogError("unknown token when expecting an expresssion");
  }
}

// binary expression
std::map<char, int> binOpPrecedence = {
    // 1 is lowest precedence
    {'<', 10},
    {'+', 20},
    {'-', 20},
    {'*', 40},
    {'/', 40}};

static int getTokPrecedence() {
  if (!isascii(curTok))
    return -1;
  auto it = binOpPrecedence.find(curTok);
  return it == binOpPrecedence.end() ? -1 : it->second;
}

static std::unique_ptr<ExprAST> parseUnary() {
  if (!isascii(curTok) || curTok == '(' || curTok == ',')
    return parsePrimary();

  int op = curTok;
  getNextToken();
  auto operand = parseUnary();
  LogDebug("parseUnary %c\n", (char)op);
  if (operand)
    return std::make_unique<UnaryExprAST>(op, std::move(operand));
  return nullptr;
}

static std::unique_ptr<ExprAST> parseBinOpRhs(int exprPrec,
                                              std::unique_ptr<ExprAST> lhs) {
  while (true) {
    int tokPrec = getTokPrecedence();
    if (tokPrec < exprPrec)
      return lhs;

    int binOp = curTok;
    getNextToken();
    // parse the unary expression after the binary operator
    auto rhs = parseUnary();
    if (!rhs)
      return nullptr;
    int nextPrec = getTokPrecedence();
    LogDebug("parseBinOpRhs: op %c(%d) prec %d\n", (char)binOp, binOp, tokPrec);
    if (tokPrec < nextPrec) {
      rhs = parseBinOpRhs(tokPrec + 1, std::move(rhs));
      if (!rhs)
        return nullptr;
    }
    lhs =
        std::make_unique<BinaryExprAST>(binOp, std::move(lhs), std::move(rhs));
  }
}

static std::unique_ptr<ExprAST> parseExpr() {
  auto lhs = parseUnary();
  if (!lhs)
    return nullptr;
  return parseBinOpRhs(0, std::move(lhs));
}

std::unique_ptr<PrototypeAST> parsePrototype() {
  std::string fnName;
  unsigned kind = 0, binPrecedence = 30; // between +- & */
  switch (curTok) {
  case tok_identifier:
    fnName = identifierStr;
    kind = 0;
    getNextToken();
    break;
  case tok_binary:
    getNextToken();
    if (!isascii(curTok))
      return LogErrorP("expecting binary operator");
    fnName = "binary";
    fnName += (char)curTok;
    kind = 2;
    getNextToken();
    if (curTok == tok_number) { // get precedence if present
      if (numVal < 1 || numVal > 100)
        return LogErrorP("invalid precedence: should be in [1, 100]");
      binPrecedence = (unsigned)numVal;
      getNextToken();
    }
    break;
  case tok_unary:
    getNextToken();
    if (!isascii(curTok))
      return LogErrorP("expecting unary operator");
    fnName = "unary";
    fnName += (char)curTok;
    kind = 1;
    getNextToken();
    break;
  default:
    return LogErrorP("expected function name in prototype");
  }

  if (curTok != '(')
    return LogErrorP("expected '(' in prototype");
  std::vector<std::string> argNames;
  while (getNextToken() == tok_identifier)
    argNames.push_back(identifierStr);

  if (curTok != ')')
    return LogErrorP("expected ')' in prototype");
  getNextToken(); // eat )

  // verify right number of args for operators
  if (kind && argNames.size() != kind)
    return LogErrorP("invalid number of operands for operator");
  LogDebug("parsePrototype %s(%d args)\n", fnName.c_str(), argNames.size());
  return std::make_unique<PrototypeAST>(fnName, std::move(argNames), kind != 0,
                                        binPrecedence);
}

std::unique_ptr<FunctionAST> parseDefinition() {
  getNextToken();
  auto proto = parsePrototype();
  if (!proto)
    return nullptr;

  auto expr = parseExpr();
  if (!expr)
    return nullptr;
  LogDebug("parseDefinition %s\n", proto->getName().c_str());
  return std::make_unique<FunctionAST>(std::move(proto), std::move(expr));
}

std::unique_ptr<PrototypeAST> parseExtern() {
  getNextToken();
  return parsePrototype();
}

std::unique_ptr<FunctionAST> parseTopLevelExpr() {
  if (auto expr = parseExpr()) {
    auto proto = std::make_unique<PrototypeAST>(ANON_EXPR_NAME,
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(proto), std::move(expr));
  }
  return nullptr;
}
