#include "parser.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "codegen.hpp"
#include "expressions.hpp"
#include "lexer.hpp"

// logerror* - these are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *str) {
  fprintf(stderr, "logerror: %s\n", str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *str) {
  LogError(str);
  return nullptr;
}

Parser::Parser() {
  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;  // highest.
}

// parse number expression
std::unique_ptr<ExprAST> Parser::ParseNumberExpr() {
  auto result = std::make_unique<NumberExprAST>(getCurrentToken().NumVal);
  getNextToken();
  return std::move(result);
}
// parse '(' , ')'
std::unique_ptr<ExprAST> Parser::ParseParenExpr() {
  getNextToken();
  auto v = ParseExpression();
  if (!v) return nullptr;

  if (getCurrentToken().type != ')') return LogError("expected ')'");
  getNextToken();

  return v;
}
// parse Identifier expression
std::unique_ptr<ExprAST> Parser::ParseIdentifierExpr() {
  std::string IdName = getCurrentToken().IdentifierStr;
  getNextToken();

  // simple variable case
  if (getCurrentToken().type != '(')  // not function. simple variable
    return std::make_unique<VariableExprAST>(IdName);

  // call faunction
  getNextToken();
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (getCurrentToken().type != ')') {
    while (1) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (getCurrentToken().type == ')') break;

      if (getCurrentToken().type != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

// parse primary expression
// identifier expression, number expression and parent expression
std::unique_ptr<ExprAST> Parser::ParsePrimary() {
  switch (getCurrentToken().type) {
    default:
      return LogError("unknown token when expecting an expression");
    case (int)::tok_identifier:
      return ParseIdentifierExpr();
    case (int)::tok_number:
      return ParseNumberExpr();
    case '(':
      return ParseParenExpr();
  }
}

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
int Parser::GetTokPrecedence() {
  if (!isascii(getCurrentToken().type)) return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[getCurrentToken().type];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}
// parse binary operator
std::unique_ptr<ExprAST> Parser::ParseBinOpRHS(int ExprPrec,
                                               std::unique_ptr<ExprAST> LHS) {
  while (1) {
    int TokPrec = GetTokPrecedence();

    if (TokPrec < ExprPrec) return LHS;

    int BinOp = getCurrentToken().type;
    getNextToken();

    auto RHS = ParsePrimary();
    if (!RHS) return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS) return nullptr;
    }

    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

// parse expression
std::unique_ptr<ExprAST> Parser::ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS) return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}

std::unique_ptr<FunctionAST> Parser::ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

// Parse prototype
std::unique_ptr<PrototypeAST> Parser::ParsePrototype() {
  if (getCurrentToken().type != (int)::tok_identifier)
    return LogErrorP("Expected function name in prototype");

  std::string FnName = getCurrentToken().IdentifierStr;
  getNextToken();

  if (getCurrentToken().type != '(')
    return LogErrorP("Expected '(' in prototype");

  std::vector<std::string> ArgNames;
  while (getNextToken() == (int)::tok_identifier)
    ArgNames.push_back(getCurrentToken().IdentifierStr);
  if (getCurrentToken().type != ')')
    return LogErrorP("Expected ')' in prototype");

  // success.
  getNextToken();  // eat ')'.

  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

std::unique_ptr<FunctionAST> Parser::ParseDefinition() {
  getNextToken();
  auto Proto = ParsePrototype();
  if (!Proto) return nullptr;

  if (auto E = ParseExpression()) {
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

std::unique_ptr<PrototypeAST> Parser::ParseExtern() {
  getNextToken();
  return ParsePrototype();
}
