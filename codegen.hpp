#pragma once

#include <map>
#include <string>

#include "KaleidoscopeJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"

class ExprAST;
class NumberExprAST;
class VariableExprAST;
class BinaryExprAST;
class CallExprAST;
class IfExprAST;
class FunctionAST;
class PrototypeAST;

class CodeGenVisitor {
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::map<std::string, llvm::Value*> NamedValues;
  std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
  std::unique_ptr<llvm::LLVMContext> TheContext;

 public:
  CodeGenVisitor();
  void InitializeModuleAndPassManager();

  std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
  std::unique_ptr<llvm::Module> TheModule;
  std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
  llvm::Value* Visit(NumberExprAST&);
  llvm::Value* Visit(VariableExprAST&);
  llvm::Value* Visit(BinaryExprAST&);
  llvm::Value* Visit(CallExprAST&);
  llvm::Value* Visit(IfExprAST&);
  llvm::Function* Visit(PrototypeAST&);
  llvm::Function* Visit(FunctionAST&);

  llvm::Function* getFunction(std::string);
};

std::unique_ptr<ExprAST> LogError(const char* Str);
std::unique_ptr<PrototypeAST> LogErrorP(const char* Str);
