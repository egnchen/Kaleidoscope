#include "ast.h"

#include <map>
#include <memory>
#include <vector>

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

static std::unique_ptr<LLVMContext> theContext;
static std::unique_ptr<Module> theModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> namedValues;

void initModule() {
  theContext = std::make_unique<LLVMContext>();
  theModule = std::make_unique<Module>("Kaleidoscope-jit", *theContext);
  Builder = std::make_unique<IRBuilder<>>(*theContext);
}

Value *LogErrorV(const char *str) {
  LogError(str);
  return nullptr;
}

Value *NumberExprAST::codegen() {
  return ConstantFP::get(*theContext, APFloat(val));
}

Value *VariableExprAST::codegen() {
  Value *v = namedValues[name];
  if (!v)
    LogErrorV("unknown variable name");
  return v;
}

Value *BinaryExprAST::codegen() {
  Value *l = lhs->codegen();
  Value *r = rhs->codegen();
  if (!l || !r)
    return nullptr;
  switch (op) {
  case '+':
    return Builder->CreateFAdd(l, r, "addtmp");
  case '-':
    return Builder->CreateFSub(l, r, "subtmp");
  case '*':
    return Builder->CreateFMul(l, r, "multmp");
  case '<':
    l = Builder->CreateFCmpULT(l, r, "cmptmp");
    // convert bool 0/1 to double 0.0/1.0
    return Builder->CreateUIToFP(l, Type::getDoubleTy(*theContext), "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

Value *CallExprAST::codegen() {
  Function *calleeF = theModule->getFunction(callee);
  if (!calleeF)
    return LogErrorV("unknown function referenced");

  if (calleeF->arg_size() != args.size())
    return LogErrorV("incorrect # of arguments passed");

  std::vector<Value *> argVs;
  for (auto &arg : args) {
    argVs.push_back(arg->codegen());
    if (!argVs.back())
      return nullptr;
  }
  return Builder->CreateCall(calleeF, argVs, "calltmp");
}

Function *PrototypeAST::codegen() {
  std::vector<Type *> doubles(args.size(), Type::getDoubleTy(*theContext));
  FunctionType *ft =
      FunctionType::get(Type::getDoubleTy(*theContext), doubles, false);
  Function *f =
      Function::Create(ft, Function::ExternalLinkage, name, theModule.get());
  unsigned idx = 0;
  for (auto &arg : f->args())
    arg.setName(args[idx++]);
  return f;
}

Function *FunctionAST::codegen() {
  Function *theFunc = theModule->getFunction(proto->getName());
  if (!theFunc)
    theFunc = proto->codegen();
  if (!theFunc)
    return nullptr;
  if (!theFunc->empty())
    return (Function *)LogErrorV("function cannot be redefined");

  BasicBlock *bb = BasicBlock::Create(*theContext, "entry", theFunc);
  Builder->SetInsertPoint(bb);

  namedValues.clear();
  for (auto &arg : theFunc->args())
    namedValues[arg.getName().str()] = &arg;

  if (Value *retVal = body->codegen()) {
    Builder->CreateRet(retVal);
    verifyFunction(*theFunc);
    return theFunc;
  } else {
    theFunc->eraseFromParent();
    return nullptr;
  }
}
