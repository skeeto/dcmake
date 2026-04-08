#include "highlight.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

ImU32 token_color(TokenType type)
{
    switch (type) {
    case TokenType::COMMENT:        return IM_COL32(106, 153, 85,  255);
    case TokenType::STRING:         return IM_COL32(206, 145, 120, 255);
    case TokenType::VARIABLE:       return IM_COL32(86,  206, 209, 255);
    case TokenType::GENERATOR_EXPR: return IM_COL32(190, 132, 209, 255);
    case TokenType::KEYWORD:        return IM_COL32(86,  156, 214, 255);
    case TokenType::BOOLEAN:        return IM_COL32(181, 206, 168, 255);
    default:                        return IM_COL32(212, 212, 212, 255);
    }
}

static bool is_cmake_keyword(std::string_view word)
{
    static const char *keywords[] = {
        "add_compile_definitions", "add_compile_options",
        "add_custom_command", "add_custom_target",
        "add_dependencies", "add_executable", "add_library",
        "add_subdirectory", "add_test",
        "break", "cmake_minimum_required", "cmake_parse_arguments",
        "cmake_path", "configure_file", "continue",
        "else", "elseif", "enable_language", "enable_testing",
        "endforeach", "endfunction", "endif", "endmacro", "endwhile",
        "execute_process", "export",
        "fetchcontent_declare", "fetchcontent_getproperties",
        "fetchcontent_makeavailable", "fetchcontent_populate",
        "file", "find_package", "find_library", "find_path",
        "find_program", "foreach", "function",
        "get_cmake_property", "get_directory_property",
        "get_filename_component", "get_property",
        "get_target_property",
        "if", "include", "include_guard", "install",
        "list",
        "macro", "mark_as_advanced", "math", "message",
        "option",
        "project",
        "return",
        "set", "set_directory_properties", "set_property",
        "set_target_properties", "string",
        "target_compile_definitions", "target_compile_features",
        "target_compile_options", "target_include_directories",
        "target_link_directories", "target_link_libraries",
        "target_link_options", "target_sources",
        "unset",
        "while",
    };
    constexpr int n = sizeof(keywords) / sizeof(keywords[0]);

    // Case-insensitive comparison via lowercase copy
    char lower[64];
    if (word.size() >= sizeof(lower)) return false;
    for (size_t i = 0; i < word.size(); i++)
        lower[i] = (char)tolower((unsigned char)word[i]);
    lower[word.size()] = 0;

    auto it = std::lower_bound(keywords, keywords + n, lower,
        [](const char *a, const char *b) { return strcmp(a, b) < 0; });
    return it != keywords + n && strcmp(*it, lower) == 0;
}

static bool is_cmake_boolean(std::string_view word)
{
    static const char *booleans[] = {
        "FALSE", "NO", "OFF", "ON", "TRUE", "YES",
    };
    constexpr int n = sizeof(booleans) / sizeof(booleans[0]);

    char upper[8];
    if (word.size() >= sizeof(upper)) return false;
    for (size_t i = 0; i < word.size(); i++)
        upper[i] = (char)toupper((unsigned char)word[i]);
    upper[word.size()] = 0;

    auto it = std::lower_bound(booleans, booleans + n, upper,
        [](const char *a, const char *b) { return strcmp(a, b) < 0; });
    return it != booleans + n && strcmp(*it, upper) == 0;
}

std::vector<Token> tokenize_cmake(std::string_view line)
{
    std::vector<Token> tokens;
    size_t i = 0;

    auto emit = [&](size_t start, size_t end, TokenType type) {
        if (end > start)
            tokens.push_back({line.substr(start, end - start), type});
    };

    // Scan for variable reference starting at i (pointing at '$').
    // Returns position after the closing '}', or i if not a variable ref.
    auto scan_variable = [&]() -> size_t {
        size_t s = i;
        size_t j = i + 1;
        // $ENV{ or $CACHE{
        if (j < line.size() && line[j] != '{') {
            size_t k = j;
            while (k < line.size() && isalpha((unsigned char)line[k])) k++;
            if (k >= line.size() || line[k] != '{') return i;
            j = k;
        }
        if (j >= line.size() || line[j] != '{') return i;
        // Scan to matching }
        int depth = 1;
        j++;
        while (j < line.size() && depth > 0) {
            if (line[j] == '{') depth++;
            else if (line[j] == '}') depth--;
            j++;
        }
        emit(s, j, TokenType::VARIABLE);
        return j;
    };

    while (i < line.size()) {
        // Comment
        if (line[i] == '#') {
            emit(i, line.size(), TokenType::COMMENT);
            i = line.size();
            break;
        }

        // String
        if (line[i] == '"') {
            size_t start = i;
            i++;
            while (i < line.size() && line[i] != '"') {
                // Variable reference inside string
                if (line[i] == '$' && i + 1 < line.size() &&
                    (line[i + 1] == '{' || isalpha((unsigned char)line[i + 1]))) {
                    emit(start, i, TokenType::STRING);
                    size_t after = scan_variable();
                    if (after > i) {
                        i = after;
                        start = i;
                        continue;
                    }
                }
                if (line[i] == '\\' && i + 1 < line.size()) i++;
                i++;
            }
            if (i < line.size()) i++; // closing quote
            emit(start, i, TokenType::STRING);
            continue;
        }

        // Variable reference outside string
        if (line[i] == '$' && i + 1 < line.size()) {
            // Generator expression
            if (line[i + 1] == '<') {
                size_t start = i;
                int depth = 1;
                i += 2;
                while (i < line.size() && depth > 0) {
                    if (line[i] == '<') depth++;
                    else if (line[i] == '>') depth--;
                    i++;
                }
                emit(start, i, TokenType::GENERATOR_EXPR);
                continue;
            }
            // Variable reference
            if (line[i + 1] == '{' || isalpha((unsigned char)line[i + 1])) {
                size_t after = scan_variable();
                if (after > i) {
                    i = after;
                    continue;
                }
            }
        }

        // Word (identifier)
        if (isalpha((unsigned char)line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.size() &&
                   (isalnum((unsigned char)line[i]) || line[i] == '_'))
                i++;
            std::string_view word = line.substr(start, i - start);
            if (is_cmake_keyword(word))
                emit(start, i, TokenType::KEYWORD);
            else if (is_cmake_boolean(word))
                emit(start, i, TokenType::BOOLEAN);
            else
                emit(start, i, TokenType::DEFAULT);
            continue;
        }

        // Default: accumulate non-special characters
        {
            size_t start = i;
            while (i < line.size() && line[i] != '#' && line[i] != '"' &&
                   line[i] != '$' && !isalpha((unsigned char)line[i]) &&
                   line[i] != '_')
                i++;
            emit(start, i, TokenType::DEFAULT);
        }
    }

    return tokens;
}
