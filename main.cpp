#include "common.h"
#include "parser.h"
#include "llvm/Support/TargetSelect.h"

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double x) {
  fputc((char)x, stderr);
  return 0;
}

/// printd - print double
extern "C" DLLEXPORT double printd(double x) {
  fprintf(stdout, "%f\n", x);
  return 0;
}

int main(void) {
  InitializeNativeTarget();
  InitializeNativeTargetAsmParser();
  InitializeNativeTargetAsmPrinter();

  initJIT();
  initModuleAndPassMgr();

  mainLoop();
  return 0;
}