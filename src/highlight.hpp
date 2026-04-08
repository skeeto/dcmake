#ifndef HIGHLIGHT_HPP
#define HIGHLIGHT_HPP

#include <string_view>
#include <vector>

#include <imgui.h>

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

ImU32 token_color(TokenType type);
std::vector<Token> tokenize_cmake(std::string_view line);

#endif
