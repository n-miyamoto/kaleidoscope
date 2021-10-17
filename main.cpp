#include <iostream>
#include <memory>
#include <vector>
#include <map>

#include "lexer.hpp"
#include "codegen.hpp"
#include "expressions.hpp"


/****************************** 
* global
*******************************/
/// BinopPrecedence - This holds the precedence for each binary operator that is defined.
static std::map<char, int> BinopPrecedence;
class CodeGenVisitor codegen;

// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "LogError: %s\n", Str);
  return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}


/****************************** 
* Parser
*******************************/
static std::unique_ptr<ExprAST> ParseExpression();

// parse number expression 
static std::unique_ptr<ExprAST> ParseNumberExpr(){
  auto result = std::make_unique<NumberExprAST>(getCurrentToken().NumVal);
  getNextToken();
  return std::move(result);
}
//parse '(' , ')'
static std::unique_ptr<ExprAST> ParseParenExpr(){
  getNextToken();
  auto v = ParseExpression();
  if(!v) return nullptr;

  if (getCurrentToken().type != ')') return LogError("expected ')'");
  getNextToken();

  return v;
}
// parse Identifier expression
static std::unique_ptr<ExprAST> ParseIdentifierExpr(){
  std::string IdName = getCurrentToken().IdentifierStr;
  
  getNextToken();

  // simple variable case
  if(getCurrentToken().type != '(') //not function. simple variable
    return std::make_unique<VariableExprAST>(IdName);

  // call faunction
  getNextToken();
  std::vector<std::unique_ptr<ExprAST>> Args;
  if(getCurrentToken().type != ')'){
    while(1){
      if(auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else return nullptr;

      if(getCurrentToken().type == ')')
        break;
      
      if(getCurrentToken().type != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

// parse primary expression
// identifier expression, number expression and parent expression
static std::unique_ptr<ExprAST> ParsePrimary(){
  switch (getCurrentToken().type){
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
static int GetTokPrecedence() {
  if (!isascii(getCurrentToken().type))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[getCurrentToken().type];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}
//parse binary operator
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS){
  while(1){
    int TokPrec = GetTokPrecedence();

    if( TokPrec < ExprPrec) return LHS;

    int BinOp = getCurrentToken().type;
    getNextToken();

    auto RHS = ParsePrimary();
    if(!RHS) return nullptr;

    int NextPrec = GetTokPrecedence();
    if(TokPrec < NextPrec){
      RHS = ParseBinOpRHS(TokPrec + 1 ,std::move(RHS));
      if(!RHS) return nullptr;
    }

    LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

// parse expression
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr(){
  if (auto E = ParseExpression()){
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

//Parse prototype
static std::unique_ptr<PrototypeAST> ParsePrototype() {
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
  getNextToken(); // eat ')'.

  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition(){
  getNextToken();
  auto Proto = ParsePrototype();
  if(!Proto) return nullptr;

  if(auto E = ParseExpression()){
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;

}

static std::unique_ptr<PrototypeAST> ParseExtern(){
  getNextToken();
  return ParsePrototype();
}



static void HandleDefinition() {
  if (auto FnAst = ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
    //if(auto *FnIR = FnAst->codegen()){
    if(auto *FnIR = FnAst->Accept(codegen) ){
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");

      codegen.TheJIT->addModule(std::move(codegen.TheModule));
      codegen.InitializeModuleAndPassManager();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern(){
  if(auto ProtoAST = ParseExtern()){
    fprintf(stderr, "Parsed an extern\n");
    //if(auto *FnIR = ProtoAST->codegen()){
    if(auto *FnIR = ProtoAST->Accept(codegen)){
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());
      codegen.FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  }else{
    getNextToken();
  }

}
static void HandleTopLevelExpression(){
  if(auto FnAST = ParseTopLevelExpr()){
    fprintf(stderr, "Parsed a top-level expr\n");
    //if(auto *FnIR = FnAST->codegen()){
    if(auto *FnIR = FnAST->Accept(codegen)){
      //print IR
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());

      auto H = codegen.TheJIT->addModule(std::move(codegen.TheModule));
      codegen.InitializeModuleAndPassManager();

      auto ExprSymbol = codegen.TheJIT->findSymbol("__anon_expr");
      assert(ExprSymbol && "function not found");

      auto e = ExprSymbol.getAddress();
      //error check
      auto error = e.takeError();
      if( (bool)error == true )
        fprintf(stderr, "ERROR!!!\n");

      double (*FP)() = (double (*)())(intptr_t)e.get();
      fprintf(stderr, "Evaluated to %f\n", FP());

      codegen.TheJIT->removeModule(H);
    }
  }else {
    getNextToken();
  }
}

static void MainLoop() {
  while(1){
    fprintf(stderr, "ready> ");
    switch(getCurrentToken().type){
    case (int)tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case (int)tok_def:
      HandleDefinition();
      break;
    case (int)tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

int main(){
  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;  // highest.

  fprintf(stderr, "ready> ");
  getNextToken();
  

  MainLoop();

  return 0;
}