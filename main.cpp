#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <map>

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
};

// Expression class for numeric literals
class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double Val) : Val(Val){}
};

// Expression class for referencing a variable 
class VariableExprAST : public ExprAST {
  std::string Name;
public:
  VariableExprAST(const std::string &Name) : Name(Name){}
};

// Expression class for a binary operator
class BinaryExprAST : public ExprAST{
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
    : Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

// Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
    : Callee(Callee), Args(std::move(Args)) {}
};

// This class represents the "prototype" for a function,
// which captures its name, and its argument names (thus implicitly the number
// of arguments the function takes).
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

public:
  PrototypeAST(const std::string &name, std::vector<std::string> Args)
    : Name(name), Args(std::move(Args)) {}

  const std::string &getName() const { return Name; }
};

// This class represents a function definition itself.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "LogError: %s\n", Str);
  return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
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

static void HandleDefinition() {
  if (ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern(){
  if(ParseExtern()){
    fprintf(stderr, "Parsed an extern\n");
  }else{
    getNextToken();
  }

}
static void HandleTopLevelExpression(){
  if(ParseTopLevelExpr()){

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

  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;  // highest.

  fprintf(stderr, "ready> ");
  getNextToken();
  MainLoop();

  while(true){
    int token_id = gettok();
    std::cout << "token_id: " << token_id << " str: " << IdentifierStr << " val " << NumVal << std::endl;
  }

  return 0;
}