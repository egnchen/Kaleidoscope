#include "codegen.h"
#include "jit.h"

#include <map>
#include <memory>
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
    return LogErrorV("invalid binary operator");
  }
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
