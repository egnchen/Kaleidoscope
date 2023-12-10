#include "codegen.h"
#include "common.h"
#include "parser.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Error.h"

static ExitOnError exitOnError;

static void handleDefinition() {
  if (auto FuncAST = parseDefinition()) {
    if (auto *FuncIR = FuncAST->codegen()) {
      LogInfo("function definition:\n");
      FuncIR->print(errs());
      LogInfo("\n");
      exitOnError(theJIT->addModule(
          orc::ThreadSafeModule(std::move(theModule), std::move(theContext))));
      initModuleAndPassMgr();
    }
  } else {
    getNextToken(); // skip next token
  }
}

static void handleExtern() {
  if (auto ProtoAST = parseExtern()) {
    if (auto *FuncIR = ProtoAST->codegen()) {
      LogInfo("extern function:\n");
      FuncIR->print(errs());
      LogInfo("\n");
      functionProtos[ProtoAST->getName()] = std::move(ProtoAST);
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

      auto rt = theJIT->getMainJITDylib().createResourceTracker();
      auto tsm =
          orc::ThreadSafeModule(std::move(theModule), std::move(theContext));
      exitOnError(theJIT->addModule(std::move(tsm), rt));
      initModuleAndPassMgr();

      // Search the JIT for the __anon_expr symbol.
      auto exprSymbol = exitOnError(theJIT->lookup(ANON_EXPR_NAME));
      assert(exprSymbol.getAddress() && "Function not found");

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native function.
      double (*fp)() = exprSymbol.getAddress().toPtr<double (*)(void)>();
      fprintf(stderr, "Evaluated to %f\n", fp());

      // Delete the anonymous expression module from the JIT.
      exitOnError(rt->remove());
    } else {
      getNextToken(); // skip next token
    }
  }
}

void mainLoop() {
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