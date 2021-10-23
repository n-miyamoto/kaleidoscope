#pragma once
#include <string>

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum TokenType {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

struct Token {
  int type;
  std::string IdentifierStr;  // Filled in if tok_identifier
  double NumVal;              // Filled in if tok_number
};

// return Token struct from std input.
Token gettok();
int getNextToken();
Token& getCurrentToken();
