include Defines.mk
.PHONY: all clean

CXX = g++

LLVM_CONFIG = ${LLVM_DIR}/llvm-config
CXXFLAGS = $(shell ${LLVM_CONFIG} --cxxflags) -std=c++17 -g -Og
LDFLAGS = $(shell ${LLVM_CONFIG} --ldflags --system-libs --libs core)

all: main 

main: main.o parser.o ast.o codegen.o runner.o
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf *.o main 
