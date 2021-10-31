#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "codegen.hpp"
#include "expressions.hpp"
#include "lexer.hpp"
#include "parser.hpp"

/******************************
 * global
 *******************************/
/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
CodeGenVisitor codegen;
Parser parser;

static void HandleDefinition() {
  if (auto FnAst = parser.ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
    // if(auto *FnIR = FnAst->codegen()){
    if (auto *FnIR = FnAst->Accept(codegen)) {
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

static void HandleExtern() {
  if (auto ProtoAST = parser.ParseExtern()) {
    fprintf(stderr, "Parsed an extern\n");
    // if(auto *FnIR = ProtoAST->codegen()){
    if (auto *FnIR = ProtoAST->Accept(codegen)) {
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());
      codegen.FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    getNextToken();
  }
}
static void HandleTopLevelExpression() {
  if (auto FnAST = parser.ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr\n");
    // if(auto *FnIR = FnAST->codegen()){
    if (auto *FnIR = FnAST->Accept(codegen)) {
      // print IR
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());

      auto H = codegen.TheJIT->addModule(std::move(codegen.TheModule));
      codegen.InitializeModuleAndPassManager();

      auto ExprSymbol = codegen.TheJIT->findSymbol("__anon_expr");
      assert(ExprSymbol && "function not found");

      auto e = ExprSymbol.getAddress();
      // error check
      auto error = e.takeError();
      if ((bool)error == true) fprintf(stderr, "ERROR!!!\n");

      double (*FP)() = (double (*)())(intptr_t)e.get();
      fprintf(stderr, "Evaluated to %f\n", FP());

      codegen.TheJIT->removeModule(H);
    }
  } else {
    getNextToken();
  }
}

static void MainLoop() {
  while (1) {
    fprintf(stderr, "ready> ");
    switch (getCurrentToken().type) {
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

int main() {
  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();

  return 0;
}
