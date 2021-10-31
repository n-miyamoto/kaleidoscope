#include "lexer.hpp"

#include <string>

/******************************
 * Lexer
 *******************************/

static Token CurTok;
static int LastChar = ' ';

Token gettok() {
  Token tk;

  // Skip any whitespace.
  while (isspace(LastChar)) LastChar = getchar();

  if (isalpha(LastChar)) {  // identifier: [a-zA-Z][a-zA-Z0-9]*
    tk.IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar()))) tk.IdentifierStr += LastChar;

    if (tk.IdentifierStr == "def") {
      tk.type = (int)tok_def;
    } else if (tk.IdentifierStr == "extern") {
      tk.type = (int)tok_extern;
    } else if (tk.IdentifierStr == "if") {
      tk.type = (int)tok_if;
    } else if (tk.IdentifierStr == "then") {
      tk.type = (int)tok_then;
    } else if (tk.IdentifierStr == "else") {
      tk.type = (int)tok_else;
    } else {
      tk.type = (int)tok_identifier;
    }

    return tk;
  }

  if (isdigit(LastChar) || LastChar == '.') {  // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    tk.NumVal = strtod(NumStr.c_str(), 0);
    tk.type = (int)::tok_number;
    return tk;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF) return gettok();
  }
  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF) {
    tk.type = (int)::tok_eof;
    return tk;
  }

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = getchar();
  tk.type = ThisChar;
  return tk;
}

int getNextToken() {
  CurTok = gettok();
  return CurTok.type;
}

Token& getCurrentToken() { return CurTok; }
