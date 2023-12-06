#include <memory>
#include <string>
#include <vector>

class ExprAST {
  public:
    virtual ~ExprAST() {}
};

class NumberExprAST : public ExprAST {
    double val;

  public:
    NumberExprAST(double val) : val(val) {}
};

class VariableExprAST : public ExprAST {
    std::string name;

  public:
    VariableExprAST(std::string_view name) : name(name) {}
};

class BinaryExprAST : public ExprAST {
    char op;
    std::unique_ptr<ExprAST> lhs, rhs;

  public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> lhs,
                  std::unique_ptr<ExprAST> rhs)
        : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

class CallExprAST : public ExprAST {
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;

  public:
    CallExprAST(std::string_view callee,
                std::vector<std::unique_ptr<ExprAST>> &&args)
        : callee(callee), args(std::move(args)) {}
};

class PrototypeAST {
    std::string name;
    std::vector<std::string> args;

  public:
    PrototypeAST(std::string_view name, std::vector<std::string> &&args)
        : name(name), args(std::move(args)) {}
};

class FunctionAST {
    std::unique_ptr<PrototypeAST> proto;
    std::unique_ptr<ExprAST> body;

  public:
    FunctionAST(std::unique_ptr<PrototypeAST> proto,
                std::unique_ptr<ExprAST> body)
        : proto(std::move(proto)), body(std::move(body)) {}
};
