#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <map>

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "KaleidoscopeJIT.h"


/*
global
*/
static std::unique_ptr<llvm::LLVMContext> TheContext;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::unique_ptr<llvm::Module> TheModule;
static std::map<std::string, llvm::Value *> NamedValues;
static std::unique_ptr<llvm::legacy::FunctionPassManager>  TheFPM;
static std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
static llvm::ExitOnError ExitOnErr;

// LogError* - These are little helper functions for error handling.
llvm::Value *LogErrorV(const char *Str);
llvm::Function *getFunction(std::string Name);
/******************************
* Lexer
*******************************/

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number

static int gettok() {
  static int LastChar = ' ';

  // Skip any whitespace.
  while (isspace(LastChar))
    LastChar = getchar();

  if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    
    return tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') {   // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  if (LastChar == '#')
  {
    // Comment until end of line.
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }
  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}

/****************************** 
* Parser
*******************************/
// Base class for all expression
class ExprAST{
public:
  virtual ~ExprAST(){}
  virtual llvm::Value *codegen() = 0;
};

// Expression class for numeric literals
class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double Val) : Val(Val){}
  virtual llvm::Value *codegen();
};

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Val));
}

// Expression class for referencing a variable 
class VariableExprAST : public ExprAST {
  std::string Name;
public:
  VariableExprAST(const std::string &Name) : Name(Name){}
  virtual llvm::Value *codegen();
};

llvm::Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  llvm::Value *V = NamedValues[Name];
  if (!V)
    LogErrorV("Unknown variable name");
  return V;
}

// Expression class for a binary operator
class BinaryExprAST : public ExprAST{
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;
  virtual llvm::Value *codegen();

public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
    : Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};
llvm::Value *BinaryExprAST::codegen() {
  llvm::Value *L = LHS->codegen();
  llvm::Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext),
                                "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}





// Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;
  virtual llvm::Value *codegen();

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
    : Callee(Callee), Args(std::move(Args)) {}
};

llvm::Value *CallExprAST::codegen(){
  llvm::Function *CalleeF = getFunction(Callee);
  //llvm::Function *CalleeF = TheModule-> getFunction(Callee);
  if(!CalleeF) return LogErrorV("Unknown function referenced");
  if(CalleeF->arg_size() != Args.size()) return LogErrorV("Incorrect #arguments passed");

  std::vector<llvm::Value*> ArgsV; 
  for(unsigned i=0, e=Args.size(); i !=e; i++){
    ArgsV.push_back( Args[i]->codegen() );
    if (!ArgsV.back()) return nullptr;
  }
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

// This class represents the "prototype" for a function,
// which captures its name, and its argument names (thus implicitly the number
// of arguments the function takes).
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

public:
  llvm::Function *codegen();
  PrototypeAST(const std::string &name, std::vector<std::string> Args)
    : Name(name), Args(std::move(Args)) {}

  const std::string &getName() const { return Name; }
};

llvm::Function *PrototypeAST::codegen(){

  std::vector<llvm::Type*>  Doubles(Args.size(),llvm::Type::getDoubleTy(*TheContext));

  llvm::FunctionType *FT = 
    llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext), Doubles, false);

  llvm::Function *F =
    llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

  unsigned Idx = 0;
  for(auto &Arg : F->args()){
    Arg.setName(Args[Idx++]);
  }

  return F;
}

// This class represents a function definition itself.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  llvm::Function *codegen();
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

llvm::Function *FunctionAST::codegen() {
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  llvm::Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  if(!TheFunction)
    TheFunction = Proto->codegen();

  if(!TheFunction)
    return nullptr;

  if(!TheFunction->empty())
    return (llvm::Function*) LogErrorV("Function cannot be redefined.");

  llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();
  for (auto &arg: TheFunction->args()){
    NamedValues[arg.getName()] = &arg;
  }

    
  if(llvm::Value *RetVal = Body->codegen()){
    Builder->CreateRet(RetVal);
    llvm::verifyFunction(*TheFunction);

    // Optimize the function.
    TheFPM->run(*TheFunction);
    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}

// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "LogError: %s\n", Str);
  return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

llvm::Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

// parse number expression 
static std::unique_ptr<ExprAST> ParseNumberExpr(){
  auto result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(result);
}
//parse '(' , ')'
static std::unique_ptr<ExprAST> ParseParenExpr(){
  getNextToken();
  auto v = ParseExpression();
  if(!v) return nullptr;

  if (CurTok != ')') return LogError("expected ')'");
  getNextToken();

  return v;
}
// parse Identifier expression
static std::unique_ptr<ExprAST> ParseIdentifierExpr(){
  std::string IdName = IdentifierStr;
  
  getNextToken();

  // simple variable case
  if(CurTok != '(') //not function. simple variable
    return std::make_unique<VariableExprAST>(IdName);

  // call faunction
  getNextToken();
  std::vector<std::unique_ptr<ExprAST>> Args;
  if(CurTok != ')'){
    while(1){
      if(auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else return nullptr;

      if(CurTok == ')')
        break;
      
      if(CurTok != ',')
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
  switch (CurTok){
    default:
      return LogError("unknown token when expecting an expression");
    case tok_identifier:
      return ParseIdentifierExpr();
    case tok_number:
      return ParseNumberExpr();
    case '(':
      return ParseParenExpr();
  }
}

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}
//parse binary operator
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS){
  while(1){
    int TokPrec = GetTokPrecedence();

    if( TokPrec < ExprPrec) return LHS;

    int BinOp = CurTok;
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
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken();

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
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

llvm::Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

static void InitializeModuleAndPassManager() {
  // Open a new context and module.
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);
  TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());

  //Create a new pass manager
  TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());
  //add pathe
  TheFPM->add(llvm::createInstructionCombiningPass());
  TheFPM->add(llvm::createReassociatePass());
  TheFPM->add(llvm::createGVNPass());
  TheFPM->add(llvm::createCFGSimplificationPass());

  TheFPM->doInitialization();

  // Create a new builder for the module.
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}


static void HandleDefinition() {
  if (auto FnAst = ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
    if(auto *FnIR = FnAst->codegen()){
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");

      TheJIT->addModule(std::move(TheModule));
      InitializeModuleAndPassManager();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern(){
  if(auto ProtoAST = ParseExtern()){
    fprintf(stderr, "Parsed an extern\n");
    if(auto *FnIR = ProtoAST->codegen()){
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  }else{
    getNextToken();
  }

}
static void HandleTopLevelExpression(){
  if(auto FnAST = ParseTopLevelExpr()){
    fprintf(stderr, "Parsed a top-level expr\n");
    if(auto *FnIR = FnAST->codegen()){
      //print IR
      fprintf(stderr, "Read function definition.\n");
      FnIR->print(llvm::errs());

      auto H = TheJIT->addModule(std::move(TheModule));
      InitializeModuleAndPassManager();

      auto ExprSymbol = TheJIT->findSymbol("__anon_expr");
      assert(ExprSymbol && "function not found");

      auto e = ExprSymbol.getAddress();
      //error check
      auto error = e.takeError();
      if( (bool)error == true )
        fprintf(stderr, "ERROR!!!\n");

      double (*FP)() = (double (*)())(intptr_t)e.get();
      fprintf(stderr, "Evaluated to %f\n", FP());

      TheJIT->removeModule(H);
    }
  }else {
    getNextToken();
  }
}

static void MainLoop() {
  while(1){
    fprintf(stderr, "ready> ");
    switch(CurTok){
    case tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}
int main(){
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;  // highest.

  fprintf(stderr, "ready> ");
  getNextToken();
  
  //TheJIT = ExitOnErr(llvm::orc::KaleidoscopeJIT()::Create());
  TheJIT = std::make_unique<llvm::orc::KaleidoscopeJIT>();
  InitializeModuleAndPassManager();

  MainLoop();

  while(true){
    int token_id = gettok();
    std::cout << "token_id: " << token_id << " str: " << IdentifierStr << " val " << NumVal << std::endl;
  }

  return 0;
}