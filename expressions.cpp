#include "expressions.hpp"

llvm::Value* NumberExprAST::Accept(CodeGenVisitor& v) { return v.Visit(*this); }

llvm::Value* VariableExprAST::Accept(CodeGenVisitor& v) {
  return v.Visit(*this);
}

llvm::Value* BinaryExprAST::Accept(CodeGenVisitor& v) { return v.Visit(*this); }

llvm::Value* IfExprAST::Accept(CodeGenVisitor& v) { return v.Visit(*this); }

llvm::Value* CallExprAST::Accept(CodeGenVisitor& v) { return v.Visit(*this); }

llvm::Function* PrototypeAST::Accept(CodeGenVisitor& v) {
  return v.Visit(*this);
}

llvm::Function* FunctionAST::Accept(CodeGenVisitor& v) {
  return v.Visit(*this);
}
