#pragma once

#include <map>

#include "ast.h"

extern std::map<std::string, std::unique_ptr<PrototypeAST>> functionProtos;