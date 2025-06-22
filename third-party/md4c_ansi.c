#include "md4c_ansi.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define snprintf _snprintf
#endif

/* ANSI Color Codes */
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"
#define ANSI_ITALIC "\033[3m"
#define ANSI_UNDERLINE "\033[4m"
#define ANSI_STRIKETHROUGH "\033[9m"
#define ANSI_INVERSE "\033[7m"

#define ANSI_BLACK "\033[30m"
#define ANSI_RED "\033[38;2;242;40;60m"
#define ANSI_GREEN "\033[38;2;0;174;107m"
#define ANSI_YELLOW "\033[38;2;255;194;0m"
#define ANSI_BLUE "\033[38;2;39;125;255m"
#define ANSI_MAGENTA "\033[38;2;215;46;130m"
#define ANSI_CYAN "\033[38;2;135;90;251m"
#define ANSI_WHITE "\033[37m"
#define ANSI_ORANGE "\033[38;2;255;122;0m"

#define ANSI_BRIGHT_BLACK "\033[90m"
#define ANSI_BRIGHT_RED "\033[91m"
#define ANSI_BRIGHT_GREEN "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"
#define ANSI_BRIGHT_BLUE "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN "\033[96m"
#define ANSI_BRIGHT_WHITE "\033[97m"

#define ANSI_BG_BLACK "\033[40m"
#define ANSI_BG_RED "\033[41m"
#define ANSI_BG_GREEN "\033[42m"
#define ANSI_BG_YELLOW "\033[43m"
#define ANSI_BG_BLUE "\033[44m"
#define ANSI_BG_MAGENTA "\033[45m"
#define ANSI_BG_CYAN "\033[46m"
#define ANSI_BG_WHITE "\033[47m"

#define BOX_H "─"
#define BOX_V "│"
#define BOX_TL "┌"
#define BOX_TR "┐"
#define BOX_BL "└"
#define BOX_BR "┘"
#define BOX_CROSS "┼"
#define BOX_T_DOWN "┬"
#define BOX_T_UP "┴"
#define BOX_T_RIGHT "├"
#define BOX_T_LEFT "┤"

#define ISDIGIT(ch) ('0' <= (ch) && (ch) <= '9')
#define ISLOWER(ch) ('a' <= (ch) && (ch) <= 'z')
#define ISUPPER(ch) ('A' <= (ch) && (ch) <= 'Z')
#define ISALNUM(ch) (ISLOWER(ch) || ISUPPER(ch) || ISDIGIT(ch))

typedef enum {
  TOKEN_NORMAL,
  TOKEN_KEYWORD,
  TOKEN_STRING,
  TOKEN_COMMENT,
  TOKEN_NUMBER,
  TOKEN_OPERATOR,
  TOKEN_IDENTIFIER,
  TOKEN_PREPROCESSOR,
  TOKEN_BOOLEAN,
  TOKEN_NULL,
  TOKEN_FUNCTION,
  TOKEN_TYPE,
  TOKEN_CONSTANT
} token_type_t;

typedef struct MD_ANSI_tag MD_ANSI;

typedef struct {
  char content[256];
  int length;
  int align;
} table_cell_t;

typedef struct {
  table_cell_t cells[64];
  int cell_count;
} table_row_t;

struct MD_ANSI_tag {
  void (*process_output)(const MD_CHAR *, MD_SIZE, void *);
  void *userdata;
  unsigned flags;
  int image_nesting_level;
  int list_level;
  int quote_level;
  int table_column;
  int table_row;
  int table_cols[64];
  int table_col_count;
  int table_aligns[64];
  int line_start;
  int in_table_header;
  int in_paragraph;
  int in_list_item;
  int in_code_block;
  int code_line_number;
  int code_content_width;
  int in_string;
  int in_comment;
  int string_escape_next;
  table_row_t table_rows[128];
  int table_row_count;
  int in_table;
  int table_header_rows;
  char table_cell_content[256];
  int table_cell_length;
};

static const char *keywords[] = {
    "if",     "else",     "elif",     "endif",    "while",     "for",
    "do",     "break",    "continue", "switch",   "case",      "default",
    "goto",   "return",   "yield",    "await",    "try",       "catch",
    "except", "finally",  "throw",    "raise",    "with",      "in",
    "is",     "function", "def",      "fn",       "func",      "lambda",
    "async",  "import",   "include",  "from",     "as",        "namespace",
    "using",  "package",  "module",   "export",   "require",   "new",
    "delete", "malloc",   "free",     "sizeof",   "typeof",    "instanceof",
    "this",   "self",     "super",    "base",     "override",  "virtual",
    "inline", "explicit", "public",   "private",  "protected", "static",
    "extern", "register", "volatile", "abstract", "final",     "const",
    "let",    "var",      "auto",     "and",      "or",        "not",
    "xor",    "begin",    "end",      "then",     "fi",        "done",
    "until",  "unless",   NULL};

static const char *type_keywords[] = {
    "int",     "float",   "double",  "char",     "string",   "bool",
    "boolean", "void",    "signed",  "unsigned", "short",    "long",
    "struct",  "union",   "enum",    "typedef",  "class",    "interface",
    "object",  "array",   "list",    "dict",     "map",      "set",
    "size_t",  "ssize_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t",  "int16_t", "int32_t", "int64_t",  "FILE",     "NULL",
    NULL};

static const char *boolean_null[] = {
    "true", "false", "True", "False",     "TRUE", "FALSE", "null",
    "NULL", "nil",   "None", "undefined", "NaN",  NULL};

static const char *builtin_functions[] = {
    "printf",       "scanf",         "fprintf",  "fscanf",      "sprintf",
    "sscanf",       "snprintf",      "puts",     "gets",        "fgets",
    "fputs",        "getchar",       "putchar",  "fgetc",       "fputc",
    "malloc",       "calloc",        "realloc",  "free",        "exit",
    "abort",        "atexit",        "strlen",   "strcpy",      "strncpy",
    "strcat",       "strncat",       "strcmp",   "strncmp",     "strchr",
    "strrchr",      "strstr",        "strtok",   "memcpy",      "memmove",
    "memset",       "memcmp",        "fopen",    "fclose",      "fread",
    "fwrite",       "fseek",         "ftell",    "rewind",      "feof",
    "ferror",       "atoi",          "atol",     "atof",        "strtol",
    "strtoul",      "strtod",        "abs",      "labs",        "fabs",
    "ceil",         "floor",         "round",    "sqrt",        "pow",
    "exp",          "log",           "sin",      "cos",         "tan",
    "asin",         "acos",          "atan",     "atan2",       "rand",
    "srand",        "time",          "clock",    "difftime",    "print",
    "input",        "len",           "range",    "enumerate",   "zip",
    "map",          "filter",        "reduce",   "max",         "min",
    "sum",          "all",           "any",      "sorted",      "reversed",
    "list",         "tuple",         "dict",     "set",         "str",
    "int",          "float",         "bool",     "type",        "isinstance",
    "hasattr",      "getattr",       "setattr",  "open",        "close",
    "read",         "write",         "readline", "readlines",   "writelines",
    "console",      "alert",         "confirm",  "prompt",      "parseInt",
    "parseFloat",   "isNaN",         "isFinite", "setTimeout",  "setInterval",
    "clearTimeout", "clearInterval", "JSON",     "Object",      "Array",
    "String",       "Number",        "Boolean",  "Date",        "Math",
    "RegExp",       "main",          "init",     "constructor", "destructor",
    "toString",     "valueOf",       "equals",   "hashCode",    NULL};

static const char *constants[] = {
    "PI",        "E",        "MAX_INT",      "MIN_INT",      "MAX_FLOAT",
    "MIN_FLOAT", "INFINITY", "NAN",          "EOF",          "NULL",
    "TRUE",      "FALSE",    "YES",          "NO",           "STDIN",
    "STDOUT",    "STDERR",   "EXIT_SUCCESS", "EXIT_FAILURE", NULL};

static const char *operators = "+-*/%=<>!&|^~()[]{}.,;:?@#$";

static inline void render_verbatim(MD_ANSI *r, const MD_CHAR *text,
                                   MD_SIZE size) {
  r->process_output(text, size, r->userdata);
}

#define RENDER_VERBATIM(r, verbatim) \
  render_verbatim((r), (verbatim), (MD_SIZE)(strlen(verbatim)))

static const char *get_token_color(token_type_t type, bool no_color) {
  if (no_color) return "";

  switch (type) {
    case TOKEN_KEYWORD:
      return ANSI_BLUE;
    case TOKEN_TYPE:
      return ANSI_CYAN;
    case TOKEN_STRING:
      return ANSI_GREEN;
    case TOKEN_COMMENT:
      return ANSI_DIM ANSI_WHITE;
    case TOKEN_NUMBER:
      return ANSI_ORANGE;
    case TOKEN_OPERATOR:
      return ANSI_MAGENTA;
    case TOKEN_PREPROCESSOR:
      return ANSI_YELLOW;
    case TOKEN_BOOLEAN:
      return ANSI_BOLD ANSI_ORANGE;
    case TOKEN_NULL:
      return ANSI_RED;
    case TOKEN_FUNCTION:
      return ANSI_BOLD ANSI_CYAN;
    case TOKEN_CONSTANT:
      return ANSI_BOLD ANSI_YELLOW;
    case TOKEN_IDENTIFIER:
      return "";
    default:
      return "";
  }
}

static const char *get_reset_color(bool no_color) {
  return no_color ? "" : ANSI_RESET;
}

static bool is_word_char(char c) { return isalnum(c) || c == '_'; }

static bool is_word_boundary(char ch) {
  return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '.' ||
          ch == ',' || ch == ';' || ch == ':' || ch == '!' || ch == '?' ||
          ch == '-' || ch == '_' || ch == '(' || ch == ')' || ch == '[' ||
          ch == ']' || ch == '{' || ch == '}' || ch == '"' || ch == '\'' ||
          ch == '/' || ch == '\\' || ch == '|');
}

static token_type_t classify_word(const char *word, size_t len,
                                  const char *following_chars,
                                  size_t following_len) {
  char temp[256];
  if (len >= sizeof(temp)) return TOKEN_IDENTIFIER;

  strncpy(temp, word, len);
  temp[len] = '\0';

  bool is_constant_pattern = true;
  bool has_letter = false;
  for (size_t i = 0; i < len; i++) {
    if (islower(temp[i]) ||
        (!isupper(temp[i]) && temp[i] != '_' && !isdigit(temp[i]))) {
      is_constant_pattern = false;
      break;
    }
    if (isalpha(temp[i])) {
      has_letter = true;
    }
  }

  if (is_constant_pattern && has_letter && len > 1) {
    for (int i = 0; constants[i]; i++) {
      if (strcmp(temp, constants[i]) == 0) {
        return TOKEN_CONSTANT;
      }
    }
    return TOKEN_CONSTANT;
  }

  for (int i = 0; type_keywords[i]; i++) {
    if (strcmp(temp, type_keywords[i]) == 0) {
      return TOKEN_TYPE;
    }
  }

  for (int i = 0; keywords[i]; i++) {
    if (strcmp(temp, keywords[i]) == 0) {
      return TOKEN_KEYWORD;
    }
  }

  for (int i = 0; boolean_null[i]; i++) {
    if (strcmp(temp, boolean_null[i]) == 0) {
      return strcmp(temp, "null") == 0 || strcmp(temp, "NULL") == 0 ||
                     strcmp(temp, "nil") == 0 || strcmp(temp, "None") == 0 ||
                     strcmp(temp, "undefined") == 0 || strcmp(temp, "NaN") == 0
                 ? TOKEN_NULL
                 : TOKEN_BOOLEAN;
    }
  }

  if (following_len > 0) {
    size_t i = 0;
    while (i < following_len && isspace(following_chars[i])) {
      i++;
    }

    if (i < following_len && following_chars[i] == '(') {
      for (int j = 0; builtin_functions[j]; j++) {
        if (strcmp(temp, builtin_functions[j]) == 0) {
          return TOKEN_FUNCTION;
        }
      }
      return TOKEN_FUNCTION;
    }
  }

  return TOKEN_IDENTIFIER;
}

static bool is_number_start(char c) { return isdigit(c) || c == '.'; }

static bool is_number_char(char c) {
  return isdigit(c) || c == '.' || c == 'x' || c == 'X' || c == 'e' ||
         c == 'E' || c == 'f' || c == 'F' || c == 'l' || c == 'L' || c == 'u' ||
         c == 'U' || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int calculate_text_width(const char *text, int len) {
  int width = 0;
  int i = 0;

  while (i < len) {
    if (text[i] == '\033') {
      i++;
      if (i < len && text[i] == '[') {
        i++;
        while (i < len && text[i] != 'm') i++;
        if (i < len) i++;
      }
    } else {
      width++;
      i++;
    }
  }

  return width;
}

static void highlight_code_line(const char *line, size_t line_len, MD_ANSI *r) {
  size_t i = 0;

  while (i < line_len) {
    char c = line[i];

    if (isspace(c)) {
      render_verbatim(r, &c, 1);
      r->code_content_width++;
      i++;
      continue;
    }

    if (r->in_string && r->string_escape_next) {
      render_verbatim(r, &c, 1);
      r->code_content_width++;
      r->string_escape_next = 0;
      i++;
      continue;
    }

    if (r->in_string) {
      if (c == '\\') {
        r->string_escape_next = 1;
      } else if (c == r->in_string) {
        r->in_string = 0;
        render_verbatim(r, &c, 1);
        r->code_content_width++;

        const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);
        render_verbatim(r, reset, strlen(reset));
        i++;
        continue;
      }
      render_verbatim(r, &c, 1);
      r->code_content_width++;
      i++;
      continue;
    }

    if (r->in_comment == 2) {
      if (i + 1 < line_len && line[i] == '*' && line[i + 1] == '/') {
        render_verbatim(r, "*/", 2);
        r->code_content_width += 2;
        r->in_comment = 0;

        const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);
        render_verbatim(r, reset, strlen(reset));
        i += 2;
        continue;
      } else {
        render_verbatim(r, &c, 1);
        r->code_content_width++;
        i++;
        continue;
      }
    }

    if (r->in_comment == 1) {
      render_verbatim(r, &c, 1);
      r->code_content_width++;
      i++;
      continue;
    }

    if (i + 1 < line_len && line[i] == '/' && line[i + 1] == '*') {
      const char *color =
          get_token_color(TOKEN_COMMENT, r->flags & MD_ANSI_FLAG_NO_COLOR);
      render_verbatim(r, color, strlen(color));

      size_t start = i;
      i += 2;

      while (i + 1 < line_len) {
        if (line[i] == '*' && line[i + 1] == '/') {
          i += 2;
          r->in_comment = 0;
          break;
        }
        i++;
      }

      if (r->in_comment == 0) {
        render_verbatim(r, line + start, i - start);
        r->code_content_width += (int)(i - start);
        const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);
        render_verbatim(r, reset, strlen(reset));
      } else {
        r->in_comment = 2;
        render_verbatim(r, line + start, i - start);
        r->code_content_width += (int)(i - start);
      }
      continue;
    }

    if ((i + 1 < line_len && line[i] == '/' && line[i + 1] == '/') ||
        (c == '#' && !(c == '#' && (i == 0 || isspace(line[i - 1])))) ||
        (i + 1 < line_len && line[i] == '-' && line[i + 1] == '-')) {
      const char *color =
          get_token_color(TOKEN_COMMENT, r->flags & MD_ANSI_FLAG_NO_COLOR);
      render_verbatim(r, color, strlen(color));
      render_verbatim(r, line + i, line_len - i);
      r->code_content_width += (int)(line_len - i);
      r->in_comment = 1;
      break;
    }

    if (c == '#' && (i == 0 || isspace(line[i - 1]))) {
      const char *color =
          get_token_color(TOKEN_PREPROCESSOR, r->flags & MD_ANSI_FLAG_NO_COLOR);
      const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);

      render_verbatim(r, color, strlen(color));

      size_t start = i;
      while (i < line_len && !isspace(line[i])) {
        i++;
      }

      render_verbatim(r, line + start, i - start);
      r->code_content_width += (int)(i - start);
      render_verbatim(r, reset, strlen(reset));
      continue;
    }

    if (c == '"' || c == '\'' || c == '`') {
      const char *color =
          get_token_color(TOKEN_STRING, r->flags & MD_ANSI_FLAG_NO_COLOR);
      render_verbatim(r, color, strlen(color));

      size_t start = i;
      char quote = c;
      r->in_string = quote;
      r->string_escape_next = 0;
      i++;

      while (i < line_len) {
        if (line[i] == '\\') {
          i += 2;
        } else if (line[i] == quote) {
          i++;
          r->in_string = 0;
          break;
        } else {
          i++;
        }
      }

      render_verbatim(r, line + start, i - start);
      r->code_content_width += (int)(i - start);

      if (r->in_string == 0) {
        const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);
        render_verbatim(r, reset, strlen(reset));
      }
      continue;
    }

    if (is_number_start(c) && (i == 0 || !is_word_char(line[i - 1]))) {
      const char *color =
          get_token_color(TOKEN_NUMBER, r->flags & MD_ANSI_FLAG_NO_COLOR);
      const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);

      render_verbatim(r, color, strlen(color));

      size_t start = i;
      while (i < line_len && is_number_char(line[i])) {
        i++;
      }

      render_verbatim(r, line + start, i - start);
      r->code_content_width += (int)(i - start);
      render_verbatim(r, reset, strlen(reset));
      continue;
    }

    if (strchr(operators, c)) {
      const char *color =
          get_token_color(TOKEN_OPERATOR, r->flags & MD_ANSI_FLAG_NO_COLOR);
      const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);

      render_verbatim(r, color, strlen(color));

      size_t start = i;
      if (i + 1 < line_len) {
        char next = line[i + 1];
        if ((c == '<' && next == '<') || (c == '>' && next == '>') ||
            (c == '=' && next == '=') || (c == '!' && next == '=') ||
            (c == '<' && next == '=') || (c == '>' && next == '=') ||
            (c == '&' && next == '&') || (c == '|' && next == '|') ||
            (c == '+' && next == '+') || (c == '-' && next == '-') ||
            (c == '+' && next == '=') || (c == '-' && next == '=') ||
            (c == '*' && next == '=') || (c == '/' && next == '=') ||
            (c == '%' && next == '=') || (c == '^' && next == '=') ||
            (c == '&' && next == '=') || (c == '|' && next == '=')) {
          i += 2;
        } else {
          i++;
        }
      } else {
        i++;
      }

      render_verbatim(r, line + start, i - start);
      r->code_content_width += (int)(i - start);
      render_verbatim(r, reset, strlen(reset));
      continue;
    }

    if (is_word_char(c)) {
      size_t start = i;
      while (i < line_len && is_word_char(line[i])) {
        i++;
      }

      bool is_complete_word = true;
      if (start > 0 && is_word_char(line[start - 1])) {
        is_complete_word = false;
      }
      if (i < line_len && is_word_char(line[i])) {
        is_complete_word = false;
      }

      token_type_t type = TOKEN_IDENTIFIER;
      if (is_complete_word) {
        const char *following = (i < line_len) ? &line[i] : "";
        size_t following_len = line_len - i;

        type = classify_word(line + start, i - start, following, following_len);
      }

      const char *color =
          get_token_color(type, r->flags & MD_ANSI_FLAG_NO_COLOR);
      const char *reset = get_reset_color(r->flags & MD_ANSI_FLAG_NO_COLOR);

      if (strlen(color) > 0) {
        render_verbatim(r, color, strlen(color));
        render_verbatim(r, line + start, i - start);
        render_verbatim(r, reset, strlen(reset));
      } else {
        render_verbatim(r, line + start, i - start);
      }
      r->code_content_width += (int)(i - start);
      continue;
    }

    render_verbatim(r, &c, 1);
    r->code_content_width++;
    i++;
  }
}

static void render_newline(MD_ANSI *r) {
  RENDER_VERBATIM(r, "\n");
  r->line_start = 1;
}

static void render_indent(MD_ANSI *r) {
  int i;
  for (i = 0; i < r->quote_level; i++) {
    if (r->flags & MD_ANSI_FLAG_NO_COLOR) {
      RENDER_VERBATIM(r, "│ ");
    } else {
      RENDER_VERBATIM(r, ANSI_BRIGHT_BLACK "│ " ANSI_RESET);
    }
  }
  for (i = 0; i < r->list_level; i++) {
    RENDER_VERBATIM(r, "  ");
  }
}

static void render_code_line_prefix(MD_ANSI *r) {
  char line_num[8];
  render_indent(r);

  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
    RENDER_VERBATIM(r, ANSI_BRIGHT_BLACK);
  }
  RENDER_VERBATIM(r, "│ ");

  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
    RENDER_VERBATIM(r, ANSI_DIM);
  }
  snprintf(line_num, sizeof(line_num), "%3d", r->code_line_number);
  RENDER_VERBATIM(r, line_num);

  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
    RENDER_VERBATIM(r, ANSI_BRIGHT_BLACK);
  }
  RENDER_VERBATIM(r, " │ ");

  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
    RENDER_VERBATIM(r, ANSI_RESET ANSI_BG_BLACK ANSI_WHITE);
  }

  if (r->in_string) {
    const char *color =
        get_token_color(TOKEN_STRING, r->flags & MD_ANSI_FLAG_NO_COLOR);
    render_verbatim(r, color, strlen(color));
  } else if (r->in_comment) {
    const char *color =
        get_token_color(TOKEN_COMMENT, r->flags & MD_ANSI_FLAG_NO_COLOR);
    render_verbatim(r, color, strlen(color));
  }

  r->line_start = 0;
  r->code_content_width = 0;
}

static void render_code_line_suffix(MD_ANSI *r, const int CODE_CONTENT_WIDTH) {
  int padding_needed = CODE_CONTENT_WIDTH - r->code_content_width;
  int j;
  for (j = 0; j < padding_needed; j++) {
    RENDER_VERBATIM(r, " ");
  }

  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
    RENDER_VERBATIM(r, ANSI_RESET ANSI_BRIGHT_BLACK);
  }
  RENDER_VERBATIM(r, "│");
  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
    RENDER_VERBATIM(r, ANSI_RESET);
  }
  render_newline(r);

  if (r->in_comment == 1) {
    r->in_comment = 0;
  }

  r->code_line_number++;
}

static void render_text_with_style(MD_ANSI *r, const MD_CHAR *text,
                                   MD_SIZE size, const char *style,
                                   const char *reset) {
  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR) && style) {
    render_verbatim(r, style, strlen(style));
  }
  render_verbatim(r, text, size);
  if (!(r->flags & MD_ANSI_FLAG_NO_COLOR) && reset) {
    render_verbatim(r, reset, strlen(reset));
  }
}

static unsigned hex_val(char ch) {
  if ('0' <= ch && ch <= '9') return ch - '0';
  if ('A' <= ch && ch <= 'Z')
    return ch - 'A' + 10;
  else
    return ch - 'a' + 10;
}

static void render_utf8_codepoint(MD_ANSI *r, unsigned codepoint,
                                  void (*fn_append)(MD_ANSI *, const MD_CHAR *,
                                                    MD_SIZE)) {
  static const MD_CHAR utf8_replacement_char[] = {(char)0xef, (char)0xbf,
                                                  (char)0xbd};

  unsigned char utf8[4];
  size_t n;

  if (codepoint <= 0x7f) {
    n = 1;
    utf8[0] = codepoint;
  } else if (codepoint <= 0x7ff) {
    n = 2;
    utf8[0] = 0xc0 | ((codepoint >> 6) & 0x1f);
    utf8[1] = 0x80 + ((codepoint >> 0) & 0x3f);
  } else if (codepoint <= 0xffff) {
    n = 3;
    utf8[0] = 0xe0 | ((codepoint >> 12) & 0xf);
    utf8[1] = 0x80 + ((codepoint >> 6) & 0x3f);
    utf8[2] = 0x80 + ((codepoint >> 0) & 0x3f);
  } else {
    n = 4;
    utf8[0] = 0xf0 | ((codepoint >> 18) & 0x7);
    utf8[1] = 0x80 + ((codepoint >> 12) & 0x3f);
    utf8[2] = 0x80 + ((codepoint >> 6) & 0x3f);
    utf8[3] = 0x80 + ((codepoint >> 0) & 0x3f);
  }

  if (0 < codepoint && codepoint <= 0x10ffff)
    fn_append(r, (char *)utf8, (MD_SIZE)n);
  else
    fn_append(r, utf8_replacement_char, 3);
}

static void render_entity(MD_ANSI *r, const MD_CHAR *text, MD_SIZE size,
                          void (*fn_append)(MD_ANSI *, const MD_CHAR *,
                                            MD_SIZE)) {
  if (r->flags & MD_ANSI_FLAG_VERBATIM_ENTITIES) {
    render_verbatim(r, text, size);
    return;
  }

  if (size > 3 && text[1] == '#') {
    unsigned codepoint = 0;

    if (text[2] == 'x' || text[2] == 'X') {
      MD_SIZE i;
      for (i = 3; i < size - 1; i++)
        codepoint = 16 * codepoint + hex_val(text[i]);
    } else {
      MD_SIZE i;
      for (i = 2; i < size - 1; i++)
        codepoint = 10 * codepoint + (text[i] - '0');
    }

    render_utf8_codepoint(r, codepoint, fn_append);
    return;
  } else {
    if (size == 4 && strncmp(text, "&lt;", 4) == 0) {
      fn_append(r, "<", 1);
      return;
    }
    if (size == 4 && strncmp(text, "&gt;", 4) == 0) {
      fn_append(r, ">", 1);
      return;
    }
    if (size == 5 && strncmp(text, "&amp;", 5) == 0) {
      fn_append(r, "&", 1);
      return;
    }
    if (size == 6 && strncmp(text, "&quot;", 6) == 0) {
      fn_append(r, "\"", 1);
      return;
    }
    if (size == 6 && strncmp(text, "&nbsp;", 6) == 0) {
      fn_append(r, " ", 1);
      return;
    }
  }

  fn_append(r, text, size);
}

static void render_attribute(MD_ANSI *r, const MD_ATTRIBUTE *attr,
                             void (*fn_append)(MD_ANSI *, const MD_CHAR *,
                                               MD_SIZE)) {
  int i;

  for (i = 0; attr->substr_offsets[i] < attr->size; i++) {
    MD_TEXTTYPE type = attr->substr_types[i];
    MD_OFFSET off = attr->substr_offsets[i];
    MD_SIZE size = attr->substr_offsets[i + 1] - off;
    const MD_CHAR *text = attr->text + off;

    switch (type) {
      case MD_TEXT_NULLCHAR:
        render_utf8_codepoint(r, 0x0000, render_verbatim);
        break;
      case MD_TEXT_ENTITY:
        render_entity(r, text, size, fn_append);
        break;
      default:
        fn_append(r, text, size);
        break;
    }
  }
}

static void render_horizontal_rule(MD_ANSI *r) {
  int i;
  const char *color =
      (r->flags & MD_ANSI_FLAG_NO_COLOR) ? "" : ANSI_BRIGHT_BLACK;
  const char *reset = (r->flags & MD_ANSI_FLAG_NO_COLOR) ? "" : ANSI_RESET;

  if (r->line_start) render_indent(r);
  render_verbatim(r, color, strlen(color));
  for (i = 0; i < 60; i++) {
    RENDER_VERBATIM(r, BOX_H);
  }
  render_verbatim(r, reset, strlen(reset));
  render_newline(r);
}

static void render_heading_prefix(MD_ANSI *r, int level) {
  const char *color;
  const char *prefix;

  if (r->flags & MD_ANSI_FLAG_NO_COLOR) {
    color = "";
  } else {
    switch (level) {
      case 1:
        color = ANSI_BOLD ANSI_RED;
        break;
      case 2:
        color = ANSI_BOLD ANSI_YELLOW;
        break;
      case 3:
        color = ANSI_BOLD ANSI_GREEN;
        break;
      case 4:
        color = ANSI_BOLD ANSI_CYAN;
        break;
      case 5:
        color = ANSI_BOLD ANSI_BLUE;
        break;
      default:
        color = ANSI_BOLD ANSI_MAGENTA;
        break;
    }
  }

  switch (level) {
    case 1:
      prefix = "# ";
      break;
    case 2:
      prefix = "## ";
      break;
    case 3:
      prefix = "### ";
      break;
    case 4:
      prefix = "#### ";
      break;
    case 5:
      prefix = "##### ";
      break;
    default:
      prefix = "###### ";
      break;
  }

  if (r->line_start) render_indent(r);
  render_text_with_style(r, prefix, strlen(prefix), color, "");
}

static void render_complete_table(MD_ANSI *r) {
  int i, j, k;
  const char *color =
      (r->flags & MD_ANSI_FLAG_NO_COLOR) ? "" : ANSI_BRIGHT_BLACK;
  const char *reset = (r->flags & MD_ANSI_FLAG_NO_COLOR) ? "" : ANSI_RESET;

  if (r->table_row_count == 0) return;

  for (i = 0; i < r->table_row_count; i++) {
    for (j = 0; j < r->table_rows[i].cell_count; j++) {
      int content_width = calculate_text_width(
          r->table_rows[i].cells[j].content, r->table_rows[i].cells[j].length);
      if (content_width > r->table_cols[j]) {
        r->table_cols[j] = content_width;
      }
      if (j >= r->table_col_count) {
        r->table_col_count = j + 1;
      }
    }
  }

  if (r->line_start) render_indent(r);
  render_verbatim(r, color, strlen(color));
  RENDER_VERBATIM(r, BOX_TL);

  for (i = 0; i < r->table_col_count; i++) {
    int width = r->table_cols[i] + 2;
    for (j = 0; j < width; j++) {
      RENDER_VERBATIM(r, BOX_H);
    }
    if (i < r->table_col_count - 1) {
      RENDER_VERBATIM(r, BOX_T_DOWN);
    }
  }
  RENDER_VERBATIM(r, BOX_TR);
  render_verbatim(r, reset, strlen(reset));
  render_newline(r);

  for (i = 0; i < r->table_row_count; i++) {
    if (r->line_start) render_indent(r);
    render_verbatim(r, color, strlen(color));
    RENDER_VERBATIM(r, BOX_V);
    render_verbatim(r, reset, strlen(reset));

    for (j = 0; j < r->table_col_count; j++) {
      RENDER_VERBATIM(r, " ");

      if (j < r->table_rows[i].cell_count) {
        table_cell_t *cell = &r->table_rows[i].cells[j];
        int content_width = calculate_text_width(cell->content, cell->length);
        int cell_width = r->table_cols[j];
        int padding = cell_width - content_width;
        int left_pad = 0, right_pad = 0;

        if (padding < 0) padding = 0;

        switch (cell->align) {
          case MD_ALIGN_LEFT:
          default:
            right_pad = padding;
            break;
          case MD_ALIGN_RIGHT:
            left_pad = padding;
            break;
          case MD_ALIGN_CENTER:
            left_pad = padding / 2;
            right_pad = padding - left_pad;
            break;
        }

        for (k = 0; k < left_pad; k++) RENDER_VERBATIM(r, " ");
        render_verbatim(r, cell->content, cell->length);
        for (k = 0; k < right_pad; k++) RENDER_VERBATIM(r, " ");
      } else {
        for (k = 0; k < r->table_cols[j]; k++) RENDER_VERBATIM(r, " ");
      }

      RENDER_VERBATIM(r, " ");
      render_verbatim(r, color, strlen(color));
      RENDER_VERBATIM(r, BOX_V);
      render_verbatim(r, reset, strlen(reset));
    }
    render_newline(r);

    if (i == r->table_header_rows - 1 && i < r->table_row_count - 1) {
      if (r->line_start) render_indent(r);
      render_verbatim(r, color, strlen(color));
      RENDER_VERBATIM(r, BOX_T_RIGHT);

      for (j = 0; j < r->table_col_count; j++) {
        int width = r->table_cols[j] + 2;
        for (k = 0; k < width; k++) {
          RENDER_VERBATIM(r, BOX_H);
        }
        if (j < r->table_col_count - 1) {
          RENDER_VERBATIM(r, BOX_CROSS);
        }
      }
      RENDER_VERBATIM(r, BOX_T_LEFT);
      render_verbatim(r, reset, strlen(reset));
      render_newline(r);
    }
  }

  if (r->line_start) render_indent(r);
  render_verbatim(r, color, strlen(color));
  RENDER_VERBATIM(r, BOX_BL);

  for (i = 0; i < r->table_col_count; i++) {
    int width = r->table_cols[i] + 2;
    for (j = 0; j < width; j++) {
      RENDER_VERBATIM(r, BOX_H);
    }
    if (i < r->table_col_count - 1) {
      RENDER_VERBATIM(r, BOX_T_UP);
    }
  }
  RENDER_VERBATIM(r, BOX_BR);
  render_verbatim(r, reset, strlen(reset));
  render_newline(r);
}

static int enter_block_callback(MD_BLOCKTYPE type, void *detail,
                                void *userdata) {
  MD_ANSI *r = (MD_ANSI *)userdata;

  switch (type) {
    case MD_BLOCK_DOC:
      break;

    case MD_BLOCK_QUOTE:
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      r->quote_level++;
      break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      if (!(r->flags & MD_ANSI_FLAG_COMPACT) && r->list_level == 0)
        render_newline(r);
      r->list_level++;
      break;

    case MD_BLOCK_LI: {
      MD_BLOCK_LI_DETAIL *li = (MD_BLOCK_LI_DETAIL *)detail;
      const char *bullet;

      r->in_list_item = 1;

      if (r->line_start) render_indent(r);

      if (li->is_task) {
        if (r->flags & MD_ANSI_FLAG_NO_COLOR) {
          bullet =
              (li->task_mark == 'x' || li->task_mark == 'X') ? "[x] " : "[ ] ";
        } else {
          bullet = (li->task_mark == 'x' || li->task_mark == 'X')
                       ? ANSI_GREEN "[x] " ANSI_RESET
                       : ANSI_RED "[ ] " ANSI_RESET;
        }
      } else {
        bullet = "• ";
      }

      RENDER_VERBATIM(r, bullet);
      r->line_start = 0;
      break;
    }

    case MD_BLOCK_HR:
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      render_horizontal_rule(r);
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      break;

    case MD_BLOCK_H: {
      MD_BLOCK_H_DETAIL *h = (MD_BLOCK_H_DETAIL *)detail;
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      render_heading_prefix(r, h->level);
      break;
    }

    case MD_BLOCK_CODE: {
      MD_BLOCK_CODE_DETAIL *code = (MD_BLOCK_CODE_DETAIL *)detail;

      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);

      if (r->line_start) render_indent(r);

      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_BRIGHT_BLACK);
      }
      RENDER_VERBATIM(r, "┌─");

      if (code->lang.text != NULL) {
        RENDER_VERBATIM(r, "[ ");
        if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
          RENDER_VERBATIM(r, ANSI_CYAN);
        }
        render_attribute(r, &code->lang, render_verbatim);
        if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
          RENDER_VERBATIM(r, ANSI_BRIGHT_BLACK);
        }
        RENDER_VERBATIM(r, " ]─");
      }

      int i;
      int total_border_width = 47;
      int used_width = 2;
      if (code->lang.text != NULL) {
        used_width += 4 + (int)code->lang.size;
      }

      for (i = used_width; i < total_border_width; i++) {
        RENDER_VERBATIM(r, "─");
      }
      RENDER_VERBATIM(r, "┐");

      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      render_newline(r);

      r->in_code_block = 1;
      r->code_line_number = 1;
      r->code_content_width = 0;
      r->line_start = 1;

      r->in_string = 0;
      r->in_comment = 0;
      r->string_escape_next = 0;
      break;
    }

    case MD_BLOCK_HTML:
      break;

    case MD_BLOCK_P:
      if (!(r->flags & MD_ANSI_FLAG_COMPACT) && !r->in_paragraph)
        render_newline(r);
      r->in_paragraph = 1;
      break;

    case MD_BLOCK_TABLE:
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      r->table_row = 0;
      r->table_column = 0;
      r->table_col_count = 0;
      r->table_row_count = 0;
      r->in_table = 1;
      r->table_header_rows = 0;
      memset(r->table_cols, 0, sizeof(r->table_cols));
      memset(r->table_aligns, 0, sizeof(r->table_aligns));
      memset(r->table_rows, 0, sizeof(r->table_rows));
      break;

    case MD_BLOCK_THEAD:
      r->in_table_header = 1;
      break;

    case MD_BLOCK_TBODY:
      r->in_table_header = 0;
      break;

    case MD_BLOCK_TR:
      r->table_column = 0;
      break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
      MD_BLOCK_TD_DETAIL *td = (MD_BLOCK_TD_DETAIL *)detail;

      r->table_cell_content[0] = '\0';
      r->table_cell_length = 0;

      if (r->table_column < 64) {
        r->table_aligns[r->table_column] = td->align;
      }
      break;
    }
  }

  return 0;
}

static int leave_block_callback(MD_BLOCKTYPE type, void *detail,
                                void *userdata) {
  MD_ANSI *r = (MD_ANSI *)userdata;

  switch (type) {
    case MD_BLOCK_DOC:
      render_newline(r);
      break;

    case MD_BLOCK_QUOTE:
      r->quote_level--;
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      r->list_level--;
      if (!(r->flags & MD_ANSI_FLAG_COMPACT) && r->list_level == 0)
        render_newline(r);
      break;

    case MD_BLOCK_LI:
      r->in_list_item = 0;
      render_newline(r);
      break;

    case MD_BLOCK_HR:
      break;

    case MD_BLOCK_H: {
      const char *reset = (r->flags & MD_ANSI_FLAG_NO_COLOR) ? "" : ANSI_RESET;
      render_verbatim(r, reset, strlen(reset));
      render_newline(r);
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      break;
    }

    case MD_BLOCK_CODE: {
      if (!r->line_start) {
        const int CODE_CONTENT_WIDTH = 60;
        int padding_needed = CODE_CONTENT_WIDTH - r->code_content_width;
        int j;
        for (j = 0; j < padding_needed; j++) {
          RENDER_VERBATIM(r, " ");
        }

        if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
          RENDER_VERBATIM(r, ANSI_RESET ANSI_BRIGHT_BLACK);
        }
        RENDER_VERBATIM(r, "│");
        if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
          RENDER_VERBATIM(r, ANSI_RESET);
        }
        render_newline(r);
      }

      if (r->line_start) render_indent(r);

      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_BRIGHT_BLACK);
      }
      RENDER_VERBATIM(r, "└");
      int i;
      for (i = 0; i < 47; i++) {
        RENDER_VERBATIM(r, "─");
      }
      RENDER_VERBATIM(r, "┘");

      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      render_newline(r);

      r->in_code_block = 0;
      r->code_line_number = 0;
      r->code_content_width = 0;

      r->in_string = 0;
      r->in_comment = 0;
      r->string_escape_next = 0;

      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      break;
    }

    case MD_BLOCK_HTML:
      break;

    case MD_BLOCK_P:
      render_newline(r);
      r->in_paragraph = 0;
      break;

    case MD_BLOCK_TABLE:
      render_complete_table(r);
      r->in_table = 0;
      if (!(r->flags & MD_ANSI_FLAG_COMPACT)) render_newline(r);
      break;

    case MD_BLOCK_THEAD:
      if (r->in_table_header) {
        r->table_header_rows = r->table_row_count;
      }
      break;

    case MD_BLOCK_TBODY:
      break;

    case MD_BLOCK_TR:
      if (r->table_row_count < 128) {
        r->table_rows[r->table_row_count].cell_count = r->table_column;
        r->table_row_count++;
      }
      r->table_row++;
      break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
      if (r->table_row_count < 128 && r->table_column < 64) {
        table_cell_t *cell =
            &r->table_rows[r->table_row_count].cells[r->table_column];
        strncpy(cell->content, r->table_cell_content,
                sizeof(cell->content) - 1);
        cell->content[sizeof(cell->content) - 1] = '\0';
        cell->length = r->table_cell_length;
        cell->align = (r->table_column < 64) ? r->table_aligns[r->table_column]
                                             : MD_ALIGN_LEFT;
      }

      r->table_column++;
      break;
    }
  }

  return 0;
}

static int enter_span_callback(MD_SPANTYPE type, void *detail, void *userdata) {
  MD_ANSI *r = (MD_ANSI *)userdata;
  int inside_img = (r->image_nesting_level > 0);

  if (type == MD_SPAN_IMG) r->image_nesting_level++;
  if (inside_img) return 0;

  switch (type) {
    case MD_SPAN_EM:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_ITALIC);
      }
      break;

    case MD_SPAN_STRONG:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_BOLD);
      }
      break;

    case MD_SPAN_U:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_UNDERLINE);
      }
      break;

    case MD_SPAN_A: {
      MD_SPAN_A_DETAIL *a = (MD_SPAN_A_DETAIL *)detail;
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, "\033]8;;");
        render_attribute(r, &a->href, render_verbatim);
        RENDER_VERBATIM(r, "\033\\");
        RENDER_VERBATIM(r, ANSI_UNDERLINE ANSI_BLUE);
      }
      break;
    }

    case MD_SPAN_IMG: {
      MD_SPAN_IMG_DETAIL *img = (MD_SPAN_IMG_DETAIL *)detail;
      RENDER_VERBATIM(r, "[Image: ");
      break;
    }

    case MD_SPAN_CODE:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_BG_BLACK ANSI_WHITE);
      }
      RENDER_VERBATIM(r, "`");
      break;

    case MD_SPAN_DEL:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_STRIKETHROUGH);
      }
      break;

    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_YELLOW);
      }
      RENDER_VERBATIM(r, "$");
      break;

    case MD_SPAN_WIKILINK: {
      MD_SPAN_WIKILINK_DETAIL *wiki = (MD_SPAN_WIKILINK_DETAIL *)detail;
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_CYAN);
      }
      RENDER_VERBATIM(r, "[[");
      break;
    }
  }

  return 0;
}

static int leave_span_callback(MD_SPANTYPE type, void *detail, void *userdata) {
  MD_ANSI *r = (MD_ANSI *)userdata;

  if (type == MD_SPAN_IMG) r->image_nesting_level--;
  if (r->image_nesting_level > 0 && type != MD_SPAN_IMG) return 0;

  switch (type) {
    case MD_SPAN_EM:
    case MD_SPAN_STRONG:
    case MD_SPAN_U:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      break;

    case MD_SPAN_A: {
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET "\033]8;;\033\\");
      }
      break;
    }

    case MD_SPAN_IMG: {
      MD_SPAN_IMG_DETAIL *img = (MD_SPAN_IMG_DETAIL *)detail;
      RENDER_VERBATIM(r, " -> ");
      render_attribute(r, &img->src, render_verbatim);
      RENDER_VERBATIM(r, "]");
      break;
    }

    case MD_SPAN_CODE:
      RENDER_VERBATIM(r, "`");
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      break;

    case MD_SPAN_DEL:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      break;

    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
      RENDER_VERBATIM(r, "$");
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      break;

    case MD_SPAN_WIKILINK:
      RENDER_VERBATIM(r, "]]");
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      break;
  }

  return 0;
}

static void add_to_table_cell_buffer(MD_ANSI *r, const MD_CHAR *text,
                                     MD_SIZE size) {
  if (r->table_cell_length + size < sizeof(r->table_cell_content) - 1) {
    memcpy(r->table_cell_content + r->table_cell_length, text, size);
    r->table_cell_length += (int)size;
    r->table_cell_content[r->table_cell_length] = '\0';
  }
}

static int text_callback(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                         void *userdata) {
  MD_ANSI *r = (MD_ANSI *)userdata;

  if (r->in_table && (type == MD_TEXT_NORMAL || type == MD_TEXT_ENTITY)) {
    if (type == MD_TEXT_ENTITY) {
      if (size == 4 && strncmp(text, "&lt;", 4) == 0) {
        add_to_table_cell_buffer(r, "<", 1);
      } else if (size == 4 && strncmp(text, "&gt;", 4) == 0) {
        add_to_table_cell_buffer(r, ">", 1);
      } else if (size == 5 && strncmp(text, "&amp;", 5) == 0) {
        add_to_table_cell_buffer(r, "&", 1);
      } else if (size == 6 && strncmp(text, "&quot;", 6) == 0) {
        add_to_table_cell_buffer(r, "\"", 1);
      } else if (size == 6 && strncmp(text, "&nbsp;", 6) == 0) {
        add_to_table_cell_buffer(r, " ", 1);
      } else {
        add_to_table_cell_buffer(r, text, size);
      }
    } else {
      add_to_table_cell_buffer(r, text, size);
    }
    return 0;
  }

  if (r->in_code_block) {
    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
      render_newline(r);
      return 0;
    }

    MD_SIZE i = 0;
    const int CODE_CONTENT_WIDTH = 40;

    while (i < size) {
      if (r->line_start) {
        render_code_line_prefix(r);
      }

      if (text[i] == '\n') {
        render_code_line_suffix(r, CODE_CONTENT_WIDTH);
        i++;
        continue;
      }

      MD_SIZE chunk_start = i;
      MD_SIZE chunk_end = i;
      MD_SIZE last_word_boundary = i;
      int line_space_left = CODE_CONTENT_WIDTH - r->code_content_width;

      while (chunk_end < size && (chunk_end - chunk_start) < line_space_left) {
        if (text[chunk_end] == '\n') {
          break;
        }

        if (is_word_boundary(text[chunk_end])) {
          last_word_boundary = chunk_end;
        }

        chunk_end++;
      }

      MD_SIZE actual_end;
      if (chunk_end >= size) {
        actual_end = size;
      } else if (text[chunk_end] == '\n') {
        actual_end = chunk_end;
      } else if ((chunk_end - chunk_start) >= line_space_left) {
        if (last_word_boundary > chunk_start &&
            (last_word_boundary - chunk_start) < line_space_left) {
          actual_end = last_word_boundary;
        } else {
          actual_end = chunk_start + line_space_left;
        }
      } else {
        actual_end = chunk_end;
      }

      MD_SIZE chunk_len = actual_end - chunk_start;
      if (chunk_len > 0) {
        char *line_chunk = malloc(chunk_len + 1);
        if (line_chunk) {
          memcpy(line_chunk, text + chunk_start, chunk_len);
          line_chunk[chunk_len] = '\0';

          highlight_code_line(line_chunk, chunk_len, r);

          free(line_chunk);
        } else {
          render_verbatim(r, text + chunk_start, chunk_len);
          r->code_content_width += (int)chunk_len;
        }
      }

      i = actual_end;

      if (i < size && is_word_boundary(text[i]) && text[i] != '\n') {
        if (r->code_content_width >= CODE_CONTENT_WIDTH) {
          i++;
        }
      }

      if (r->code_content_width >= CODE_CONTENT_WIDTH ||
          (i < size && text[i] == '\n')) {
        render_code_line_suffix(r, CODE_CONTENT_WIDTH);
      }
    }

    return 0;
  }

  if (r->line_start && type != MD_TEXT_BR && type != MD_TEXT_SOFTBR &&
      type != MD_TEXT_HTML && size > 0 && !r->in_list_item &&
      !r->in_code_block) {
    render_indent(r);
    r->line_start = 0;
  }

  if (size > 0 && type != MD_TEXT_BR && type != MD_TEXT_SOFTBR) {
    r->line_start = 0;
  }

  switch (type) {
    case MD_TEXT_NULLCHAR:
      render_utf8_codepoint(r, 0x0000, render_verbatim);
      break;

    case MD_TEXT_BR:
      render_newline(r);
      break;

    case MD_TEXT_SOFTBR:
      if (r->image_nesting_level == 0) {
        render_newline(r);
      } else {
        RENDER_VERBATIM(r, " ");
      }
      break;

    case MD_TEXT_HTML:
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_DIM);
      }
      RENDER_VERBATIM(r, "<");
      render_verbatim(r, text, size);
      RENDER_VERBATIM(r, ">");
      if (!(r->flags & MD_ANSI_FLAG_NO_COLOR)) {
        RENDER_VERBATIM(r, ANSI_RESET);
      }
      break;

    case MD_TEXT_ENTITY:
      render_entity(r, text, size, render_verbatim);
      break;

    default:
      render_verbatim(r, text, size);
      break;
  }

  return 0;
}

static void debug_log_callback(const char *msg, void *userdata) {
  MD_ANSI *r = (MD_ANSI *)userdata;
  if (r->flags & MD_ANSI_FLAG_DEBUG) fprintf(stderr, "MD4C: %s\n", msg);
}

int md_ansi(const MD_CHAR *input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
            void *userdata, unsigned parser_flags, unsigned renderer_flags) {
  MD_ANSI render = {
      process_output,
      userdata,
      renderer_flags,
      0,   /* image_nesting_level */
      0,   /* list_level */
      0,   /* quote_level */
      0,   /* table_column */
      0,   /* table_row */
      {0}, /* table_cols */
      0,   /* table_col_count */
      {0}, /* table_aligns */
      1,   /* line_start */
      0,   /* in_table_header */
      0,   /* in_paragraph */
      0,   /* in_list_item */
      0,   /* in_code_block */
      0,   /* code_line_number */
      0,   /* code_content_width */
      0,   /* in_string */
      0,   /* in_comment */
      0,   /* string_escape_next */
      {},  /* table_rows */
      0,   /* table_row_count */
      0,   /* in_table */
      0,   /* table_header_rows */
      "",  /* table_cell_content */
      0    /* table_cell_length */
  };

  MD_PARSER parser = {0,
                      parser_flags,
                      enter_block_callback,
                      leave_block_callback,
                      enter_span_callback,
                      leave_span_callback,
                      text_callback,
                      debug_log_callback,
                      NULL};

  if (renderer_flags & MD_ANSI_FLAG_SKIP_UTF8_BOM && sizeof(MD_CHAR) == 1) {
    static const MD_CHAR bom[3] = {(char)0xef, (char)0xbb, (char)0xbf};
    if (input_size >= sizeof(bom) && memcmp(input, bom, sizeof(bom)) == 0) {
      input += sizeof(bom);
      input_size -= sizeof(bom);
    }
  }

  return md_parse(input, input_size, &parser, (void *)&render);
}