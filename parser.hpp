#pragma once

#include <map>
#include <memory>
#include <vector>

#include "expressions.hpp"

class Parser {
  std::map<char, int> BinopPrecedence;

  std::unique_ptr<ExprAST> ParseNumberExpr();
  std::unique_ptr<ExprAST> ParseParenExpr();
  std::unique_ptr<ExprAST> ParseIdentifierExpr();
  std::unique_ptr<ExprAST> ParsePrimary();
  int GetTokPrecedence();
  std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                         std::unique_ptr<ExprAST> LHS);
  std::unique_ptr<PrototypeAST> ParsePrototype();
  std::unique_ptr<ExprAST> ParseExpression();
  std::unique_ptr<ExprAST> ParseIfExpr();
  std::unique_ptr<ExprAST> ParseForExpr();

 public:
  Parser();
  std::unique_ptr<FunctionAST> ParseTopLevelExpr();
  std::unique_ptr<FunctionAST> ParseDefinition();
  std::unique_ptr<PrototypeAST> ParseExtern();
};
