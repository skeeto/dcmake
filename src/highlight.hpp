#pragma once

#include <string_view>
#include <vector>

#include "theme.hpp"

enum struct TokenType {
    DEFAULT,
    COMMENT,
    STRING,
    VARIABLE,
    GENERATOR_EXPR,
    KEYWORD,
    BOOLEAN,
};

struct Token {
    std::string_view text;
    TokenType type;
};

ImU32 token_color(TokenType type, const Palette *p);
std::vector<Token> tokenize_cmake(std::string_view line);
