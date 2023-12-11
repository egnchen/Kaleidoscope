#include "codegen.h"
#include "jit.h"
#include "parser.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "llvm/ADT/APFloat.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

static std::map<std::string, Value *> namedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> functionProtos;

std::unique_ptr<LLVMContext> theContext;
std::unique_ptr<Module> theModule;
std::unique_ptr<IRBuilder<>> Builder;
// std::unique_ptr<KaleidoscopeJIT> theJIT;
static std::unique_ptr<FunctionPassManager> theFPM;
static std::unique_ptr<LoopAnalysisManager> theLAM;
static std::unique_ptr<FunctionAnalysisManager> theFAM;
static std::unique_ptr<CGSCCAnalysisManager> theCGAM;
static std::unique_ptr<ModuleAnalysisManager> theMAM;
static std::unique_ptr<PassInstrumentationCallbacks> thePIC;
static std::unique_ptr<StandardInstrumentations> theSI;
std::unique_ptr<orc::KaleidoscopeJIT> theJIT;

void initModuleAndPassMgr() {
  theContext = std::make_unique<LLVMContext>();
  theModule = std::make_unique<Module>("Kaleidoscope-jit", *theContext);
  theModule->setDataLayout(theJIT->getDataLayout());
  Builder = std::make_unique<IRBuilder<>>(*theContext);

  // Create new pass and analysis managers
  theFPM = std::make_unique<FunctionPassManager>();
  theLAM = std::make_unique<LoopAnalysisManager>();
  theFAM = std::make_unique<FunctionAnalysisManager>();
  theCGAM = std::make_unique<CGSCCAnalysisManager>();
  theMAM = std::make_unique<ModuleAnalysisManager>();
  thePIC = std::make_unique<PassInstrumentationCallbacks>();
  theSI = std::make_unique<StandardInstrumentations>(*theContext, true);
  theSI->registerCallbacks(*thePIC, theMAM.get());

  // add transformation passes
  theFPM->addPass(InstCombinePass());
  theFPM->addPass(ReassociatePass());
  theFPM->addPass(GVNPass());
  theFPM->addPass(SimplifyCFGPass());
  PassBuilder pb;
  pb.registerModuleAnalyses(*theMAM);
  pb.registerFunctionAnalyses(*theFAM);
  pb.crossRegisterProxies(*theLAM, *theFAM, *theCGAM, *theMAM);
}

void initJIT() { theJIT = std::move(orc::KaleidoscopeJIT::Create().get()); }

Value *LogErrorV(const char *str) {
  LogError(str);
  return nullptr;
}

Function *getFunction(const std::string &name) {
  // first check if it is already in the module
  if (auto *F = theModule->getFunction(name))
    return F;

  // check if we can codegen the decl from existing prototype
  auto fi = functionProtos.find(name);
  if (fi != functionProtos.end()) {
    return fi->second->codegen();
  }

  // return null if no decl exists
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

Value *UnaryExprAST::codegen() {
  Value *operandV = operand->codegen();
  if (!operandV)
    return nullptr;
  Function *f = getFunction(std::string("unary") + op);
  if (!f)
    return LogErrorV("invalid unary operator");
  return Builder->CreateCall(f, operandV, "unop");
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
  case '/':
    return Builder->CreateFDiv(l, r, "divtmp");
  case '<':
    l = Builder->CreateFCmpULT(l, r, "cmptmp");
    // convert bool 0/1 to double 0.0/1.0
    return Builder->CreateUIToFP(l, Type::getDoubleTy(*theContext), "booltmp");
  default:
    break;
  }
  // user-defined binary operator
  Function *f = getFunction(std::string("binary") + op);
  if (!f)
    return LogErrorV("invalid binary operator");
  return Builder->CreateCall(f, {l, r}, "binop");
}

Value *CallExprAST::codegen() {
  Function *calleeF = getFunction(callee);
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

Value *IfExprAST::codegen() {
  Value *condV = cond->codegen();
  if (!condV)
    return nullptr;

  condV = Builder->CreateFCmpONE(
      condV, ConstantFP::get(*theContext, APFloat(0.0)), "ifcond");

  Function *theFunc = Builder->GetInsertBlock()->getParent();
  BasicBlock *thenBB = BasicBlock::Create(*theContext, "then");
  BasicBlock *elseBB = BasicBlock::Create(*theContext, "else");
  BasicBlock *mergeBB = BasicBlock::Create(*theContext, "endif");
  Builder->CreateCondBr(condV, thenBB, elseBB);

  // emit then node
  theFunc->insert(theFunc->end(), thenBB);
  Builder->SetInsertPoint(thenBB);
  Value *thenV = then_->codegen();
  if (!thenV)
    return nullptr;
  Builder->CreateBr(mergeBB);
  thenBB = Builder->GetInsertBlock(); // get end of then block

  // emit else node
  theFunc->insert(theFunc->end(), elseBB);
  Builder->SetInsertPoint(elseBB);
  Value *elseV = else_->codegen();
  if (!elseV)
    return nullptr;
  Builder->CreateBr(mergeBB);
  elseBB = Builder->GetInsertBlock(); // get end of else block

  // emit merge node
  theFunc->insert(theFunc->end(), mergeBB);
  Builder->SetInsertPoint(mergeBB);
  PHINode *pn = Builder->CreatePHI(Type::getDoubleTy(*theContext), 2, "iftmp");
  pn->addIncoming(thenV, thenBB);
  pn->addIncoming(elseV, elseBB);
  return pn;
}

Value *ForExprAST::codegen() {
  Value *startV = start->codegen();
  if (!startV)
    return nullptr;
  Function *theFunc = Builder->GetInsertBlock()->getParent();
  BasicBlock *preBB = Builder->GetInsertBlock();
  BasicBlock *loopBB = BasicBlock::Create(*theContext, "loop");

  Builder->CreateBr(loopBB); // implicit fall through from pre to loop
  theFunc->insert(theFunc->end(), loopBB);

  // start the loop BB with a phi node
  Builder->SetInsertPoint(loopBB);
  PHINode *loopVar =
      Builder->CreatePHI(Type::getDoubleTy(*theContext), 2, varName);
  loopVar->addIncoming(startV, preBB);
  // if the loop variable is defined before, create a new one
  // and restore it after the loop
  Value *oldVal = namedValues[varName];
  namedValues[varName] = loopVar;

  // generate body
  if (!body->codegen())
    return nullptr;

  // add the loopVar by step value, default to 1.0
  Value *stepVal = nullptr;
  if (step) {
    stepVal = step->codegen();
    if (!stepVal)
      return nullptr;
  } else
    stepVal = ConstantFP::get(*theContext, APFloat(1.0));
  Value *nextVar = Builder->CreateFAdd(loopVar, stepVal, "nextvar");

  // compute end condition and branch conditionally
  Value *endCond = end->codegen();
  if (!endCond)
    return nullptr;
  endCond = Builder->CreateFCmpONE(
      endCond, ConstantFP::get(*theContext, APFloat(0.0)), "loopcond");
  BasicBlock *loopEndBB = Builder->GetInsertBlock();
  BasicBlock *afterBB = BasicBlock::Create(*theContext, "afterloop", theFunc);
  Builder->CreateCondBr(endCond, loopBB, afterBB);

  // any new code will be inserted in afterBB
  Builder->SetInsertPoint(afterBB);

  // add loopEndBB to phi node
  loopVar->addIncoming(nextVar, loopEndBB);

  // restore old value
  if (oldVal)
    namedValues[varName] = oldVal;
  else
    namedValues.erase(varName);

  return ConstantFP::getNullValue(Type::getDoubleTy(*theContext));
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
  auto &p = *proto;
  functionProtos[p.getName()] = std::move(proto);
  Function *theFunc = getFunction(p.getName());
  if (!theFunc)
    return nullptr;
  if (!theFunc->empty())
    return (Function *)LogErrorV("function cannot be redefined");
  if (p.isBinaryOp()) // if this is a binary operator, install it
    binOpPrecedence[p.getOperatorName()] = p.getBinaryPrecedence();

  BasicBlock *bb = BasicBlock::Create(*theContext, "entry", theFunc);
  Builder->SetInsertPoint(bb);

  namedValues.clear();
  for (auto &arg : theFunc->args())
    namedValues[arg.getName().str()] = &arg;

  if (Value *retVal = body->codegen()) {
    Builder->CreateRet(retVal);
    verifyFunction(*theFunc);
    theFPM->run(*theFunc, *theFAM);
    return theFunc;
  } else {
    theFunc->eraseFromParent();
    return nullptr;
  }
}
