#include "common.h"
#include "llvm/Support/TargetSelect.h"

int main(void) {
  InitializeNativeTarget();
  InitializeNativeTargetAsmParser();
  InitializeNativeTargetAsmPrinter();

  initJIT();
  initModuleAndPassMgr();
  mainLoop();
  return 0;
}