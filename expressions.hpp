#pragma once

#include "codegen.hpp"

class ExprAST {
 public:
  virtual ~ExprAST() {}
  virtual llvm::Value *Accept(CodeGenVisitor &) = 0;
};

// Expression class for numeric literals
class NumberExprAST : public ExprAST {
 public:
  double Val;
  NumberExprAST(double Val) : Val(Val) {}
  virtual llvm::Value *Accept(CodeGenVisitor &);
};

// Expression class for referencing a variable
class VariableExprAST : public ExprAST {
 public:
  std::string Name;
  VariableExprAST(const std::string &Name) : Name(Name) {}
  virtual llvm::Value *Accept(CodeGenVisitor &);
};

// Expression class for a binary operator
class BinaryExprAST : public ExprAST {
 public:
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;
  virtual llvm::Value *Accept(CodeGenVisitor &);

  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

/// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ExprAST {
 public:
  std::unique_ptr<ExprAST> Cond, Then, Else;
  virtual llvm::Value *Accept(CodeGenVisitor &);
  IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
            std::unique_ptr<ExprAST> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
};

/// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
public:
  std::string VarName;
  std::unique_ptr<ExprAST> Start, End, Step, Body;
  virtual llvm::Value *Accept(CodeGenVisitor &);
  ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
             std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
             std::unique_ptr<ExprAST> Body)
    : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
      Step(std::move(Step)), Body(std::move(Body)) {}
};

// Expression class for function calls.
class CallExprAST : public ExprAST {
 public:
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;
  virtual llvm::Value *Accept(CodeGenVisitor &);

  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
};

// This class represents the "prototype" for a function,
// which captures its name, and its argument names (thus implicitly the number
// of arguments the function takes).
class PrototypeAST {
 public:
  std::string Name;
  std::vector<std::string> Args;
  llvm::Function *Accept(CodeGenVisitor &);
  PrototypeAST(const std::string &name, std::vector<std::string> Args)
      : Name(name), Args(std::move(Args)) {}

  const std::string &getName() const { return Name; }
};

// This class represents a function definition itself.
class FunctionAST {
 public:
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;
  llvm::Function *Accept(CodeGenVisitor &);
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
};
