#pragma once

#include "jit.h"
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

inline void LogDebug(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, fmt, args);
  va_end(args);
}

inline void LogInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, fmt, args);
  va_end(args);
}

namespace llvm {
namespace orc {
class KaleidoscopeJIT;
} // namespace orc
} // namespace llvm

extern std::unique_ptr<LLVMContext> theContext;
extern std::unique_ptr<Module> theModule;
extern std::unique_ptr<IRBuilder<>> Builder;
extern std::unique_ptr<orc::KaleidoscopeJIT> theJIT;
// constants
inline const char *ANON_EXPR_NAME = "__anon_expr";

// interfaces
void initModuleAndPassMgr();
void initJIT();
void mainLoop();