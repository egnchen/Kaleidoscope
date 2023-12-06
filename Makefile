include Defines.mk
.PHONY: all clean

CXX = g++

LLVM_CONFIG = ${LLVM_DIR}/llvm-config
CXXFLAGS = $(shell ${LLVM_CONFIG} --cxxflags) -std=c++17 -g -O2
LDFLAGS = $(shell ${LLVM_CONFIG} --ldflags)

all: lexer

lexer: lexer.o ast.o
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf *.o lexer