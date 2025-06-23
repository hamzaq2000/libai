#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE

#define TB_OPT_ATTR_W 64
#define TB_OPT_EGC
#define TB_OPT_PRINTF_BUF 8192
#define TB_OPT_READ_BUF 128

#define TB_IMPL
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "ai.h"
#include "third-party/cJSON.h"
#include "third-party/md4c_ansi.h"
#include "third-party/termbox2.h"

#define SIDEBAR_WIDTH 30
#define INPUT_MIN_HEIGHT 3
#define INPUT_MAX_HEIGHT 10
#define MIN_CHAT_WIDTH 40
#define MIN_TERM_WIDTH 90
#define MIN_TERM_HEIGHT 10
#define MAX_MESSAGE_LENGTH 4096
#define MAX_TOOL_RESPONSE_LENGTH 2048
#define MAX_TOOLS 32
#define APP_DIR_NAME ".momo"
#define MESSAGE_MARGIN 4

#define TARGET_FPS 60
#define FRAME_TIME_US (1000000 / TARGET_FPS)
#define MAX_FRAME_TIME_US (1000000 / 30)
#define UTF8_PLAIN_TEXT_TYPE CFSTR("public.utf8-plain-text")
#define PLAIN_TEXT_TYPE CFSTR("public.plain-text")

#define COLOR_BG TB_DEFAULT
#define COLOR_FG 0xE8E8E8
#define COLOR_ACCENT 0x00AAFF
#define COLOR_USER 0x4A90E2
#define COLOR_ASSISTANT 0x7ED321
#define COLOR_SYSTEM 0xF5A623
#define COLOR_ERROR 0xD0021B
#define COLOR_SUCCESS 0x50E3C2
#define COLOR_PANEL 0xF0F0F0
#define COLOR_DIM 0x888888
#define COLOR_TOOL 0xBD10E0
#define COLOR_DIVIDER 0x2D2D2D
#define COLOR_TIMESTAMP 0x666666
#define COLOR_METADATA 0x999999
#define COLOR_BORDER 0x404040
#define COLOR_HIGHLIGHT 0x3D4043
#define COLOR_USER_BG 0x1A1D29
#define COLOR_ASSISTANT_BG 0x1A2617
#define COLOR_SYSTEM_BG 0x292317
#define COLOR_JSON_KEY 0x66D9EF
#define COLOR_JSON_STRING 0xA6E22E
#define COLOR_JSON_NUMBER 0xAE81FF
#define COLOR_JSON_BOOLEAN 0xF92672
#define COLOR_JSON_NULL 0x75715E
#define COLOR_JSON_BRACE 0xF8F8F2
#define COLOR_JSON_COLON 0xF8F8F2
#define COLOR_LABEL_USER 0x5DADE2
#define COLOR_LABEL_ASSISTANT 0x58D68D
#define COLOR_LABEL_SYSTEM 0xF39C12
#define COLOR_LABEL_TOOL_EXEC 0xE74C3C
#define COLOR_LABEL_TOOL_RESP 0x9B59B6
#define COLOR_LOGO_DARK 0x4A5568
#define COLOR_LOGO_LIGHT 0x8BB9E8

#define MESSAGE_PADDING 2
#define MESSAGE_SEPARATOR_HEIGHT 1
#define HEADER_SPACING 2

const uint32_t momo_line1[] = {0x2588, 0x2588, 0x2588, 0x0020, 0x0020,
                               0x0020, 0x0020, 0x2588, 0x2588, 0x2588,
                               0x0020, 0x0020, 0x2588, 0x2588, 0x2588,
                               0x2588, 0x2588, 0x2588, 0x0020, 0x0000};
const uint32_t momo_line2[] = {0x2588, 0x2588, 0x2588, 0x2588, 0x0020,
                               0x0020, 0x2588, 0x2588, 0x2588, 0x2588,
                               0x0020, 0x2588, 0x2588, 0x0020, 0x0020,
                               0x0020, 0x0020, 0x2588, 0x2588, 0x0000};
const uint32_t momo_line3[] = {0x2588, 0x2588, 0x0020, 0x2588, 0x2588,
                               0x2588, 0x2588, 0x0020, 0x2588, 0x2588,
                               0x0020, 0x2588, 0x2588, 0x0020, 0x0020,
                               0x0020, 0x0020, 0x2588, 0x2588, 0x0000};
const uint32_t momo_line4[] = {0x2588, 0x2588, 0x0020, 0x0020, 0x2588,
                               0x2588, 0x0020, 0x0020, 0x2588, 0x2588,
                               0x0020, 0x2588, 0x2588, 0x0020, 0x0020,
                               0x0020, 0x0020, 0x2588, 0x2588, 0x0000};
const uint32_t momo_line5[] = {0x2588, 0x2588, 0x0020, 0x0020, 0x0020,
                               0x0020, 0x0020, 0x0020, 0x2588, 0x2588,
                               0x0020, 0x0020, 0x2588, 0x2588, 0x2588,
                               0x2588, 0x2588, 0x2588, 0x0020, 0x0000};

typedef struct {
  struct timeval last_frame;
  struct timeval current_frame;
  long frame_delta_us;
  int fps_counter;
  time_t fps_last_second;
  int current_fps;
  float smooth_fps;
  bool first_frame;
} frame_timing_t;

typedef struct {
  char *type;
  char *command;
  char **args;
  int arg_count;
  char **env;
  int env_count;
} mcp_config_t;

typedef struct {
  char *name;
  char *description;
  char *input_schema;
  mcp_config_t *mcp;
  bool is_builtin;
} tool_config_t;

typedef enum { STATE_WELCOME, STATE_CHAT, STATE_SETTINGS } app_state_t;

typedef enum {
  MSG_USER,
  MSG_ASSISTANT,
  MSG_SYSTEM,
  MSG_TOOL_CALL,
  MSG_TOOL_RESPONSE
} message_type_t;

typedef enum {
  RESPONSE_MODE_NORMAL,
  RESPONSE_MODE_TOOLS_ENABLED
} response_mode_t;

typedef struct tool_execution {
  char *tool_name;
  char *parameters;
  char *response;
  struct tool_execution *next;
} tool_execution_t;

typedef struct {
  bool enable_tools;
  bool enable_history;
  bool enable_guardrails;
  response_mode_t response_mode;
  double temperature;
  int max_tokens;
} session_config_t;

typedef struct rendered_line {
  char *text;
  uintattr_t color;
  struct rendered_line *next;
} rendered_line_t;

typedef struct message {
  message_type_t type;
  char *content;
  char *tool_name;
  time_t timestamp;
  bool is_streaming;
  tool_execution_t *tool_executions;
  rendered_line_t *lines;
  int line_count;
  bool needs_rerender;
  struct message *next;
} message_t;

typedef struct message_update {
  message_t *target_message;
  char *new_content;
  bool is_streaming;
  tool_execution_t *new_tool_executions;
  bool process_markdown;
  struct message_update *next;
} message_update_t;

typedef struct {
  message_update_t *head;
  message_update_t *tail;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} update_queue_t;

typedef struct {
  bool active;
  ai_stream_id_t stream_id;
  char *accumulated_text;
  size_t accumulated_length;
  bool waiting_for_stream;
  pthread_mutex_t mutex;
} streaming_context_t;

typedef struct {
  int thinking_frame;
  int cursor_blink_frame;
  int loading_dots_frame;
  long animation_timer_us;
  bool show_cursor;
} animation_state_t;

typedef struct {
  int scroll_offset;
  bool auto_scroll;
  int total_lines;
  int visible_lines;
} chat_display_t;

static struct {
  int term_width;
  int term_height;
  bool running;
  bool needs_resize;
  frame_timing_t timing;
  animation_state_t animation;
  ai_context_t *ai_context;
  ai_session_id_t ai_session;
  ai_availability_t ai_availability;
  char *availability_reason;
  app_state_t state;
  session_config_t session_config;
  tool_config_t tools[MAX_TOOLS];
  int tool_count;
  char *tools_json;
  char *app_dir;
  int chat_width;
  int sidebar_x;
  int chat_height;
  int input_height;
  bool show_sidebar;
  message_t *messages;
  message_t *current_streaming;
  int message_count;
  chat_display_t chat;
  update_queue_t update_queue;
  char input_buffer[MAX_MESSAGE_LENGTH];
  int input_pos;
  int input_scroll;
  streaming_context_t streaming;
  ai_stats_t stats;
} app;

static void init_app(void);
static void cleanup_app(void);
static bool init_ai_session(void);
static void cleanup_ai_session(void);
static bool init_app_directory(void);
static char *load_schema_from_file(const char *filepath) {
  FILE *file = fopen(filepath, "r");
  if (!file) {
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    fclose(file);
    return NULL;
  }

  char *content = malloc(file_size + 1);
  if (!content) {
    fclose(file);
    return NULL;
  }

  size_t read_size = fread(content, 1, file_size, file);
  content[read_size] = '\0';
  fclose(file);

  return content;
}

static bool parse_schema_directive(const char *input, char **extracted_message,
                                   char **schema_content) {
  *extracted_message = NULL;
  *schema_content = NULL;

  const char *schema_start = strstr(input, "/schema ");
  if (!schema_start) {
    return false;
  }

  const char *filepath_start = schema_start + 8;

  while (*filepath_start == ' ' || *filepath_start == '\t') {
    filepath_start++;
  }

  if (*filepath_start == '\0') {
    return false;
  }

  const char *filepath_end = filepath_start;
  while (*filepath_end != '\0' && *filepath_end != ' ' &&
         *filepath_end != '\t' && *filepath_end != '\n') {
    filepath_end++;
  }

  size_t filepath_len = filepath_end - filepath_start;
  char *filepath = malloc(filepath_len + 1);
  if (!filepath) {
    return false;
  }
  strncpy(filepath, filepath_start, filepath_len);
  filepath[filepath_len] = '\0';

  *schema_content = load_schema_from_file(filepath);
  free(filepath);

  if (!*schema_content) {
    return false;
  }

  size_t input_len = strlen(input);
  size_t directive_start = schema_start - input;
  size_t directive_len = filepath_end - schema_start;

  size_t before_len = directive_start;
  size_t after_len = input_len - (directive_start + directive_len);

  while (*filepath_end == ' ' || *filepath_end == '\t') {
    filepath_end++;
    after_len = input_len - (filepath_end - input);
  }

  *extracted_message = malloc(before_len + after_len + 1);
  if (!*extracted_message) {
    free(*schema_content);
    *schema_content = NULL;
    return false;
  }

  strncpy(*extracted_message, input, before_len);

  strcpy(*extracted_message + before_len, filepath_end);

  char *end = *extracted_message + strlen(*extracted_message) - 1;
  while (end >= *extracted_message &&
         (*end == ' ' || *end == '\t' || *end == '\n')) {
    *end = '\0';
    end--;
  }

  return true;
}

static char *get_clipboard_text() {
  PasteboardRef pasteboard;
  OSStatus status;
  ItemCount item_count;
  PasteboardItemID item_id;
  CFDataRef clipboard_data;
  CFStringRef clipboard_string;
  char *result = NULL;

  // Create a reference to the system clipboard
  status = PasteboardCreate(kPasteboardClipboard, &pasteboard);
  if (status != noErr) {
    return NULL;
  }

  // Synchronize with the current clipboard contents
  PasteboardSynchronize(pasteboard);

  // Get the number of items on the clipboard
  status = PasteboardGetItemCount(pasteboard, &item_count);
  if (status != noErr || item_count < 1) {
    CFRelease(pasteboard);
    return NULL;
  }

  // Get the first item's ID
  status = PasteboardGetItemIdentifier(pasteboard, 1, &item_id);
  if (status != noErr) {
    CFRelease(pasteboard);
    return NULL;
  }

  // Try to get text data from the clipboard
  status = PasteboardCopyItemFlavorData(pasteboard, item_id,
                                        UTF8_PLAIN_TEXT_TYPE, &clipboard_data);
  if (status != noErr) {
    // If UTF-8 fails, try plain text
    status = PasteboardCopyItemFlavorData(pasteboard, item_id, PLAIN_TEXT_TYPE,
                                          &clipboard_data);
    if (status != noErr) {
      CFRelease(pasteboard);
      return NULL;
    }
  }

  // Convert CFData to CFString
  clipboard_string = CFStringCreateFromExternalRepresentation(
      kCFAllocatorDefault, clipboard_data, kCFStringEncodingUTF8);

  if (clipboard_string) {
    // Get the length needed for C string
    CFIndex string_length = CFStringGetLength(clipboard_string);
    CFIndex max_size = CFStringGetMaximumSizeForEncoding(
                           string_length, kCFStringEncodingUTF8) +
                       1;

    // Allocate memory and convert to C string
    result = malloc(max_size);
    if (result) {
      Boolean success = CFStringGetCString(clipboard_string, result, max_size,
                                           kCFStringEncodingUTF8);
      if (!success) {
        free(result);
        result = NULL;
      }
    }

    CFRelease(clipboard_string);
  }

  // Clean up
  CFRelease(clipboard_data);
  CFRelease(pasteboard);

  return result;
}

static void free_tools_config(void);
static void free_mcp_config(mcp_config_t *mcp);
static char *create_tools_json_for_bridge(void);
static char *execute_mcp_tool(const mcp_config_t *mcp, const char *input_json);
static char *load_schema_from_file(const char *filepath);
static bool parse_schema_directive(const char *input, char **extracted_message,
                                   char **schema_content);

static void init_update_queue(void);
static void cleanup_update_queue(void);
static void queue_message_update(message_t *msg, const char *content,
                                 bool is_streaming,
                                 tool_execution_t *tool_executions);
static void process_message_updates(void);
static message_update_t *create_message_update(
    message_t *msg, const char *content, bool is_streaming,
    tool_execution_t *tool_executions);
static void free_message_update(message_update_t *update);

static tool_execution_t *clone_tool_executions(tool_execution_t *src);
static void add_tool_execution_to_list(tool_execution_t **list,
                                       const char *tool_name,
                                       const char *parameters,
                                       const char *response);
static void free_tool_executions(tool_execution_t *executions);

static void init_frame_timing(void);
static void update_frame_timing(void);
static void wait_for_next_frame(void);

static void update_animations(long delta_us);
static void init_animations(void);

typedef struct {
  char *accumulated_output;
  size_t output_length;
  size_t output_capacity;
} markdown_render_context_t;

static void markdown_output_callback(const MD_CHAR *data, MD_SIZE data_size,
                                     void *userdata) {
  markdown_render_context_t *ctx = (markdown_render_context_t *)userdata;
  if (!ctx || !data || data_size == 0) return;

  size_t needed = ctx->output_length + data_size + 1;
  if (needed > ctx->output_capacity) {
    size_t new_capacity = ctx->output_capacity * 2;
    if (new_capacity < needed) new_capacity = needed;

    char *new_buffer = realloc(ctx->accumulated_output, new_capacity);
    if (!new_buffer) return;

    ctx->accumulated_output = new_buffer;
    ctx->output_capacity = new_capacity;
  }

  memcpy(ctx->accumulated_output + ctx->output_length, data, data_size);
  ctx->output_length += data_size;
  ctx->accumulated_output[ctx->output_length] = '\0';
}

static char *process_markdown_to_ansi(const char *markdown_content) {
  if (!markdown_content) return NULL;

  markdown_render_context_t ctx;
  ctx.output_capacity = strlen(markdown_content) * 2;
  ctx.accumulated_output = malloc(ctx.output_capacity);
  ctx.output_length = 0;

  if (!ctx.accumulated_output) return NULL;

  ctx.accumulated_output[0] = '\0';

  unsigned parser_flags = 0;
  parser_flags |= MD_FLAG_TABLES;
  parser_flags |= MD_FLAG_STRIKETHROUGH;
  parser_flags |= MD_FLAG_TASKLISTS;
  parser_flags |= MD_FLAG_LATEXMATHSPANS;
  parser_flags |= MD_FLAG_WIKILINKS;

  unsigned renderer_flags = 0;

  int result =
      md_ansi(markdown_content, strlen(markdown_content),
              markdown_output_callback, &ctx, parser_flags, renderer_flags);

  if (result != 0) {
    free(ctx.accumulated_output);
    return NULL;
  }

  return ctx.accumulated_output;
}

static void rebuild_all_message_rendering(void);
static void free_message_lines(message_t *msg);
static void add_rendered_line(message_t *msg, const char *text,
                              uintattr_t color);
static void render_content_lines(message_t *msg, const char *content,
                                 uintattr_t color, int indent);
static void render_tool_executions(message_t *msg);
static void calculate_chat_metrics(void);
static void scroll_chat(int lines);
static void scroll_to_bottom(void);
static void scroll_to_top(void);
static void draw_chat_messages(void);

static char *sanitize_utf8_string(const char *input);
static void wrap_text_to_lines(const char *text, int max_width, char ***lines,
                               int *line_count);
static bool is_json_content(const char *content);
static bool is_word_boundary(uint32_t codepoint);

static const char *get_message_label_text(message_type_t type);
static const char *get_message_label_icon(message_type_t type);
static uintattr_t get_message_label_color(message_type_t type);

static void move_cursor_up_in_input(void) {
  if (app.input_pos == 0) return;

  int line_start = app.input_pos;
  while (line_start > 0 && app.input_buffer[line_start - 1] != '\n') {
    line_start--;
  }

  if (line_start == 0) return;

  int prev_line_end = line_start - 2;
  while (prev_line_end >= 0 && app.input_buffer[prev_line_end] != '\n') {
    prev_line_end--;
  }
  int prev_line_start = prev_line_end + 1;

  int current_col = app.input_pos - line_start;

  int prev_line_len = (line_start - 1) - prev_line_start;
  int target_col = (current_col < prev_line_len) ? current_col : prev_line_len;

  app.input_pos = prev_line_start + target_col;
}

static void move_cursor_down_in_input(void) {
  int line_start = app.input_pos;
  while (line_start > 0 && app.input_buffer[line_start - 1] != '\n') {
    line_start--;
  }

  int line_end = app.input_pos;
  while (line_end < (int)strlen(app.input_buffer) &&
         app.input_buffer[line_end] != '\n') {
    line_end++;
  }

  if (line_end >= (int)strlen(app.input_buffer)) return;

  int next_line_start = line_end + 1;
  int next_line_end = next_line_start;
  while (next_line_end < (int)strlen(app.input_buffer) &&
         app.input_buffer[next_line_end] != '\n') {
    next_line_end++;
  }

  int current_col = app.input_pos - line_start;

  int next_line_len = next_line_end - next_line_start;
  int target_col = (current_col < next_line_len) ? current_col : next_line_len;

  app.input_pos = next_line_start + target_col;
}

static void move_to_line_start(void) {
  while (app.input_pos > 0 && app.input_buffer[app.input_pos - 1] != '\n') {
    app.input_pos--;
  }
}

static void move_to_line_end(void) {
  while (app.input_pos < (int)strlen(app.input_buffer) &&
         app.input_buffer[app.input_pos] != '\n') {
    app.input_pos++;
  }
}

static void move_to_previous_word(void) {
  if (app.input_pos == 0) return;

  uint32_t current_codepoint = 0;
  if (app.input_pos > 0) {
    int pos = app.input_pos - 1;
    while (pos > 0 && (app.input_buffer[pos] & 0x80) &&
           !(app.input_buffer[pos] & 0x40)) {
      pos--;
    }
    tb_utf8_char_to_unicode(&current_codepoint, app.input_buffer + pos);
  }

  if (!is_word_boundary(current_codepoint) && current_codepoint != '\n') {
    while (app.input_pos > 0) {
      int prev_pos = app.input_pos - 1;
      while (prev_pos > 0 && (app.input_buffer[prev_pos] & 0x80) &&
             !(app.input_buffer[prev_pos] & 0x40)) {
        prev_pos--;
      }

      uint32_t codepoint;
      int byte_len =
          tb_utf8_char_to_unicode(&codepoint, app.input_buffer + prev_pos);
      if (byte_len <= 0) break;

      if (is_word_boundary(codepoint) || codepoint == '\n') {
        break;
      }

      app.input_pos = prev_pos;
    }
  } else {
    while (app.input_pos > 0) {
      int prev_pos = app.input_pos - 1;
      while (prev_pos > 0 && (app.input_buffer[prev_pos] & 0x80) &&
             !(app.input_buffer[prev_pos] & 0x40)) {
        prev_pos--;
      }

      uint32_t codepoint;
      int byte_len =
          tb_utf8_char_to_unicode(&codepoint, app.input_buffer + prev_pos);
      if (byte_len <= 0) break;

      if (!is_word_boundary(codepoint) && codepoint != '\n') {
        app.input_pos = prev_pos;

        while (app.input_pos > 0) {
          int prev_pos2 = app.input_pos - 1;
          while (prev_pos2 > 0 && (app.input_buffer[prev_pos2] & 0x80) &&
                 !(app.input_buffer[prev_pos2] & 0x40)) {
            prev_pos2--;
          }

          uint32_t codepoint2;
          tb_utf8_char_to_unicode(&codepoint2, app.input_buffer + prev_pos2);

          if (is_word_boundary(codepoint2) || codepoint2 == '\n') {
            break;
          }

          app.input_pos = prev_pos2;
        }
        break;
      }

      app.input_pos = prev_pos;
    }
  }
}

static void move_to_next_word(void) {
  int input_len = strlen(app.input_buffer);
  if (app.input_pos >= input_len) return;

  uint32_t current_codepoint = 0;
  if (app.input_pos < input_len) {
    tb_utf8_char_to_unicode(&current_codepoint,
                            app.input_buffer + app.input_pos);
  }

  if (!is_word_boundary(current_codepoint) && current_codepoint != '\n') {
    while (app.input_pos < input_len) {
      uint32_t codepoint;
      int byte_len =
          tb_utf8_char_to_unicode(&codepoint, app.input_buffer + app.input_pos);
      if (byte_len <= 0) {
        app.input_pos++;
        continue;
      }

      if (is_word_boundary(codepoint) || codepoint == '\n') {
        break;
      }

      app.input_pos += byte_len;
    }
  } else {
    while (app.input_pos < input_len) {
      uint32_t codepoint;
      int byte_len =
          tb_utf8_char_to_unicode(&codepoint, app.input_buffer + app.input_pos);
      if (byte_len <= 0) {
        app.input_pos++;
        continue;
      }

      if (!is_word_boundary(codepoint) && codepoint != '\n') {
        break;
      }

      app.input_pos += byte_len;
    }
  }
};

static int calculate_input_lines(void);
static void update_input_height(void);

char *tool_get_current_time(const char *parameters_json, void *user_data);
char *tool_calculate(const char *parameters_json, void *user_data);
char *tool_system_info(const char *parameters_json, void *user_data);
char *mcp_tool_handler(const char *parameters_json, void *user_data);

static void update_dimensions(void);
static void handle_input(struct tb_event *ev);
static void draw_welcome_screen(void);
static void draw_chat_interface(void);
static void draw_sidebar(void);
static void draw_input_bar(void);
static void render_frame(void);
static void process_command(const char *input);
static void send_message(const char *message);
static void add_message(message_type_t type, const char *content);
static void start_streaming_response(const char *prompt, const char *schema);
static void streaming_callback(ai_context_t *context, const char *chunk,
                               void *user_data);
static void free_messages(void);
static char *format_time(time_t timestamp);
static void draw_logo_line(int x, int y, const uint32_t *line,
                           uintattr_t color);
static int get_unicode_line_length(const uint32_t *line);
static void show_error_with_code(ai_result_t result, const char *context_msg);
static void show_error_message(const char *message);
static void update_app_stats(void);

static volatile sig_atomic_t resize_pending = 0;

static void handle_sigwinch(int sig) {
  (void)sig;
  resize_pending = 1;
}

static void init_update_queue(void) {
  app.update_queue.head = NULL;
  app.update_queue.tail = NULL;
  pthread_mutex_init(&app.update_queue.mutex, NULL);
  pthread_cond_init(&app.update_queue.cond, NULL);
}

static void cleanup_update_queue(void) {
  pthread_mutex_lock(&app.update_queue.mutex);

  message_update_t *current = app.update_queue.head;
  while (current) {
    message_update_t *next = current->next;
    free_message_update(current);
    current = next;
  }

  app.update_queue.head = NULL;
  app.update_queue.tail = NULL;

  pthread_mutex_unlock(&app.update_queue.mutex);
  pthread_mutex_destroy(&app.update_queue.mutex);
  pthread_cond_destroy(&app.update_queue.cond);
}

static message_update_t *create_message_update(
    message_t *msg, const char *content, bool is_streaming,
    tool_execution_t *tool_executions) {
  message_update_t *update = malloc(sizeof(message_update_t));
  if (!update) return NULL;

  update->target_message = msg;
  update->new_content = content ? strdup(content) : NULL;
  update->is_streaming = is_streaming;
  update->new_tool_executions = clone_tool_executions(tool_executions);
  update->process_markdown = (content != NULL);
  update->next = NULL;

  return update;
}

static void free_message_update(message_update_t *update) {
  if (!update) return;

  if (update->new_content) free(update->new_content);

  free_tool_executions(update->new_tool_executions);
  free(update);
}

static void queue_message_update(message_t *msg, const char *content,
                                 bool is_streaming,
                                 tool_execution_t *tool_executions) {
  message_update_t *update =
      create_message_update(msg, content, is_streaming, tool_executions);
  if (!update) return;

  pthread_mutex_lock(&app.update_queue.mutex);

  if (app.update_queue.tail) {
    app.update_queue.tail->next = update;
    app.update_queue.tail = update;
  } else {
    app.update_queue.head = app.update_queue.tail = update;
  }

  pthread_cond_signal(&app.update_queue.cond);
  pthread_mutex_unlock(&app.update_queue.mutex);
}

static void render_markdown_lines(message_t *msg, const char *ansi_content) {
  if (!msg || !ansi_content) return;

  free_message_lines(msg);

  if (msg->type == MSG_ASSISTANT && msg->tool_executions) {
    render_tool_executions(msg);
    add_rendered_line(msg, "", COLOR_FG);
  }

  const char *ptr = ansi_content;
  const char *line_start = ptr;

  while (*ptr) {
    if (*ptr == '\n') {
      size_t line_len = ptr - line_start;
      char *line = malloc(line_len + 1);
      if (line) {
        strncpy(line, line_start, line_len);
        line[line_len] = '\0';
        add_rendered_line(msg, line, COLOR_FG);
        free(line);
      }

      ptr++;
      line_start = ptr;
    } else {
      ptr++;
    }
  }

  if (line_start < ptr) {
    size_t line_len = ptr - line_start;
    char *line = malloc(line_len + 1);
    if (line) {
      strncpy(line, line_start, line_len);
      line[line_len] = '\0';
      add_rendered_line(msg, line, COLOR_FG);
      free(line);
    }
  }

  if (msg->is_streaming) {
    pthread_mutex_lock(&app.streaming.mutex);
    bool waiting = app.streaming.waiting_for_stream;
    bool active = app.streaming.active;
    pthread_mutex_unlock(&app.streaming.mutex);

    if (active) {
      if (waiting) {
        const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
        char thinking[32];
        snprintf(thinking, sizeof(thinking), "  %s Thinking...",
                 frames[app.animation.thinking_frame]);
        add_rendered_line(msg, thinking, COLOR_ACCENT | TB_BOLD);
      } else {
        if (app.animation.show_cursor) {
          add_rendered_line(msg, "  ▋", COLOR_ACCENT);
        }
      }
    }
  }

  msg->needs_rerender = false;
}

static void process_message_updates(void) {
  pthread_mutex_lock(&app.update_queue.mutex);

  message_update_t *current = app.update_queue.head;
  app.update_queue.head = NULL;
  app.update_queue.tail = NULL;

  pthread_mutex_unlock(&app.update_queue.mutex);

  while (current) {
    message_update_t *next = current->next;

    if (current->target_message) {
      if (current->new_content) {
        if (current->target_message->content)
          free(current->target_message->content);
        current->target_message->content = strdup(current->new_content);
      }

      current->target_message->is_streaming = current->is_streaming;

      if (current->new_tool_executions) {
        free_tool_executions(current->target_message->tool_executions);
        current->target_message->tool_executions =
            clone_tool_executions(current->new_tool_executions);
      }

      if (current->process_markdown && current->new_content) {
        char *rendered_markdown =
            process_markdown_to_ansi(current->new_content);
        if (rendered_markdown) {
          render_markdown_lines(current->target_message, rendered_markdown);
          free(rendered_markdown);
        } else {
          current->target_message->needs_rerender = true;
        }
      } else {
        current->target_message->needs_rerender = true;
      }
    }

    free_message_update(current);
    current = next;
  }
}

static tool_execution_t *clone_tool_executions(tool_execution_t *src) {
  if (!src) return NULL;

  tool_execution_t *head = NULL;
  tool_execution_t *tail = NULL;

  tool_execution_t *current = src;
  while (current) {
    tool_execution_t *clone = malloc(sizeof(tool_execution_t));
    if (!clone) break;

    clone->tool_name = current->tool_name ? strdup(current->tool_name) : NULL;
    clone->parameters =
        current->parameters ? strdup(current->parameters) : NULL;
    clone->response = current->response ? strdup(current->response) : NULL;
    clone->next = NULL;

    if (tail) {
      tail->next = clone;
      tail = clone;
    } else {
      head = tail = clone;
    }

    current = current->next;
  }

  return head;
}

static void add_tool_execution_to_list(tool_execution_t **list,
                                       const char *tool_name,
                                       const char *parameters,
                                       const char *response) {
  tool_execution_t *exec = malloc(sizeof(tool_execution_t));
  if (!exec) return;

  exec->tool_name = tool_name ? strdup(tool_name) : NULL;
  exec->parameters = parameters ? strdup(parameters) : NULL;
  exec->response = response ? strdup(response) : NULL;
  exec->next = NULL;

  if (!*list) {
    *list = exec;
  } else {
    tool_execution_t *current = *list;
    while (current->next) current = current->next;
    current->next = exec;
  }
}

static void free_tool_executions(tool_execution_t *executions) {
  tool_execution_t *current = executions;
  while (current) {
    tool_execution_t *next = current->next;
    if (current->tool_name) free(current->tool_name);
    if (current->parameters) free(current->parameters);
    if (current->response) free(current->response);
    free(current);
    current = next;
  }
}

static const char *get_message_label_text(message_type_t type) {
  switch (type) {
    case MSG_USER:
      return "YOU";
    case MSG_ASSISTANT:
      return "ASSISTANT";
    case MSG_SYSTEM:
      return "SYSTEM";
    case MSG_TOOL_CALL:
      return "TOOL";
    case MSG_TOOL_RESPONSE:
      return "RESPONSE";
    default:
      return "UNKNOWN";
  }
}

static const char *get_message_label_icon(message_type_t type) {
  switch (type) {
    case MSG_USER:
      return "▶";
    case MSG_ASSISTANT:
      return "◆";
    case MSG_SYSTEM:
      return "●";
    case MSG_TOOL_CALL:
      return "⚡";
    case MSG_TOOL_RESPONSE:
      return "⚙";
    default:
      return "?";
  }
}

static uintattr_t get_message_label_color(message_type_t type) {
  switch (type) {
    case MSG_USER:
      return COLOR_LABEL_USER;
    case MSG_ASSISTANT:
      return COLOR_LABEL_ASSISTANT;
    case MSG_SYSTEM:
      return COLOR_LABEL_SYSTEM;
    case MSG_TOOL_CALL:
      return COLOR_LABEL_TOOL_EXEC;
    case MSG_TOOL_RESPONSE:
      return COLOR_LABEL_TOOL_RESP;
    default:
      return COLOR_FG;
  }
}

static void init_frame_timing(void) {
  memset(&app.timing, 0, sizeof(frame_timing_t));
  gettimeofday(&app.timing.last_frame, NULL);
  app.timing.current_fps = 60;
  app.timing.smooth_fps = 60.0f;
  app.timing.first_frame = true;
  app.timing.fps_last_second = time(NULL);
}

static void update_frame_timing(void) {
  gettimeofday(&app.timing.current_frame, NULL);

  if (!app.timing.first_frame) {
    app.timing.frame_delta_us =
        (app.timing.current_frame.tv_sec - app.timing.last_frame.tv_sec) *
            1000000L +
        (app.timing.current_frame.tv_usec - app.timing.last_frame.tv_usec);

    app.timing.fps_counter++;
    time_t current_second = time(NULL);
    if (current_second != app.timing.fps_last_second) {
      app.timing.current_fps = app.timing.fps_counter;
      app.timing.fps_counter = 0;
      app.timing.fps_last_second = current_second;

      float alpha = 0.1f;
      app.timing.smooth_fps = alpha * app.timing.current_fps +
                              (1.0f - alpha) * app.timing.smooth_fps;
    }
  } else {
    app.timing.frame_delta_us = FRAME_TIME_US;
    app.timing.first_frame = false;
  }

  app.timing.last_frame = app.timing.current_frame;
}

static void wait_for_next_frame(void) {
  long sleep_time = FRAME_TIME_US - app.timing.frame_delta_us;

  if (sleep_time > 0 && sleep_time < MAX_FRAME_TIME_US) {
    usleep(sleep_time);
  }
}

static void init_animations(void) {
  memset(&app.animation, 0, sizeof(animation_state_t));
  app.animation.show_cursor = true;
}

static void update_animations(long delta_us) {
  app.animation.animation_timer_us += delta_us;

  if (app.animation.animation_timer_us >= 250000) {
    app.animation.thinking_frame = (app.animation.thinking_frame + 1) % 4;

    app.animation.cursor_blink_frame =
        (app.animation.cursor_blink_frame + 1) % 30;
    app.animation.show_cursor = app.animation.cursor_blink_frame < 15;

    app.animation.loading_dots_frame =
        (app.animation.loading_dots_frame + 1) % 4;

    if (app.current_streaming) {
      app.current_streaming->needs_rerender = true;
    }

    app.animation.animation_timer_us = 0;
  }
}

static char *sanitize_utf8_string(const char *input) {
  if (!input) return NULL;

  size_t len = strlen(input);
  char *output = malloc(len + 1);
  if (!output) return NULL;

  size_t out_pos = 0;
  const char *ptr = input;

  while (*ptr) {
    uint32_t codepoint;
    int byte_len = tb_utf8_char_to_unicode(&codepoint, ptr);

    if (byte_len <= 0) {
      ptr++;
      continue;
    }

    if (codepoint >= 32 || codepoint == '\n' || codepoint == '\t' ||
        codepoint == '\r') {
      for (int i = 0; i < byte_len; i++) {
        output[out_pos++] = ptr[i];
      }
    }

    ptr += byte_len;
  }

  output[out_pos] = '\0';
  return output;
}

static bool is_word_boundary(uint32_t codepoint) {
  return (codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
          codepoint == '\r' || codepoint == '.' || codepoint == ',' ||
          codepoint == ';' || codepoint == ':' || codepoint == '!' ||
          codepoint == '?' || codepoint == '-' || codepoint == '_' ||
          codepoint == '(' || codepoint == ')' || codepoint == '[' ||
          codepoint == ']' || codepoint == '{' || codepoint == '}' ||
          codepoint == '"' || codepoint == '\'' || codepoint == '/' ||
          codepoint == '\\' || codepoint == '|');
}

static void wrap_text_to_lines(const char *text, int max_width, char ***lines,
                               int *line_count) {
  *lines = NULL;
  *line_count = 0;

  if (!text || max_width <= 0) return;

  if (max_width < 5) max_width = 5;

  int capacity = 10;
  *lines = malloc(sizeof(char *) * capacity);
  if (!*lines) return;

  const char *ptr = text;
  const char *text_end = text + strlen(text);

  while (ptr < text_end && *ptr) {
    if (*ptr == '\n') {
      if (*line_count >= capacity) {
        capacity *= 2;
        char **new_lines = realloc(*lines, sizeof(char *) * capacity);
        if (!new_lines) {
          for (int i = 0; i < *line_count; i++) free((*lines)[i]);
          free(*lines);
          *lines = NULL;
          *line_count = 0;
          return;
        }
        *lines = new_lines;
      }
      (*lines)[*line_count] = strdup("");
      (*line_count)++;
      ptr++;
      continue;
    }

    const char *line_start = ptr;
    const char *last_break = NULL;
    int chars_count = 0;
    const char *scan_ptr = ptr;

    while (scan_ptr < text_end && *scan_ptr && *scan_ptr != '\n' &&
           chars_count < max_width) {
      uint32_t codepoint;
      int byte_len = tb_utf8_char_to_unicode(&codepoint, scan_ptr);

      if (byte_len <= 0) {
        scan_ptr++;
        chars_count++;
        continue;
      }

      if (is_word_boundary(codepoint)) {
        last_break = scan_ptr + byte_len;
      }

      scan_ptr += byte_len;
      chars_count++;
    }

    const char *line_end;

    if (chars_count < max_width || scan_ptr >= text_end || *scan_ptr == '\n') {
      line_end = scan_ptr;
      ptr = scan_ptr;
    } else if (last_break && last_break > line_start &&
               (last_break - line_start) >= max_width / 3) {
      line_end = last_break;
      ptr = last_break;

      while (ptr < text_end && (*ptr == ' ' || *ptr == '\t')) {
        ptr++;
      }
    } else {
      if (scan_ptr <= line_start) {
        uint32_t codepoint;
        int byte_len = tb_utf8_char_to_unicode(&codepoint, line_start);
        if (byte_len <= 0) byte_len = 1;
        line_end = line_start + byte_len;
        ptr = line_end;
      } else {
        line_end = scan_ptr;
        ptr = scan_ptr;
      }
    }

    size_t line_len = line_end - line_start;
    if (line_len > 0) {
      char *line_text = malloc(line_len + 1);
      if (line_text) {
        strncpy(line_text, line_start, line_len);
        line_text[line_len] = '\0';

        if (*line_count >= capacity) {
          capacity *= 2;
          char **new_lines = realloc(*lines, sizeof(char *) * capacity);
          if (!new_lines) {
            free(line_text);
            for (int i = 0; i < *line_count; i++) free((*lines)[i]);
            free(*lines);
            *lines = NULL;
            *line_count = 0;
            return;
          }
          *lines = new_lines;
        }
        (*lines)[*line_count] = line_text;
        (*line_count)++;
      }
    } else {
      if (*line_count >= capacity) {
        capacity *= 2;
        char **new_lines = realloc(*lines, sizeof(char *) * capacity);
        if (!new_lines) {
          for (int i = 0; i < *line_count; i++) free((*lines)[i]);
          free(*lines);
          *lines = NULL;
          *line_count = 0;
          return;
        }
        *lines = new_lines;
      }
      (*lines)[*line_count] = strdup("");
      (*line_count)++;
    }
  }

  if (*line_count == 0) {
    if (capacity > 0) {
      (*lines)[0] = strdup("");
      *line_count = 1;
    }
  }
}

static int calculate_input_lines(void) {
  if (strlen(app.input_buffer) == 0) return 1;

  int input_width = app.term_width - 6;
  if (input_width < 10) input_width = 10;

  char **lines;
  int line_count;
  wrap_text_to_lines(app.input_buffer, input_width, &lines, &line_count);

  for (int i = 0; i < line_count; i++) {
    free(lines[i]);
  }
  free(lines);

  return line_count > 0 ? line_count : 1;
}

static void update_input_height(void) {
  int needed_lines = calculate_input_lines();
  int new_height = needed_lines + 2;

  if (new_height < INPUT_MIN_HEIGHT) new_height = INPUT_MIN_HEIGHT;
  if (new_height > INPUT_MAX_HEIGHT) new_height = INPUT_MAX_HEIGHT;

  if (new_height != app.input_height) {
    app.input_height = new_height;
    app.chat_height = app.term_height - app.input_height - 1;

    calculate_chat_metrics();
    if (app.chat.auto_scroll) {
      scroll_to_bottom();
    }
  }
}

static void free_message_lines(message_t *msg) {
  if (!msg) return;

  rendered_line_t *line = msg->lines;
  while (line) {
    rendered_line_t *next = line->next;
    if (line->text) free(line->text);
    free(line);
    line = next;
  }
  msg->lines = NULL;
  msg->line_count = 0;
}

static void add_rendered_line(message_t *msg, const char *text,
                              uintattr_t color) {
  if (!msg || !text) return;

  rendered_line_t *new_line = malloc(sizeof(rendered_line_t));
  if (!new_line) return;

  new_line->text = strdup(text);
  new_line->color = color;
  new_line->next = NULL;

  if (!msg->lines) {
    msg->lines = new_line;
  } else {
    rendered_line_t *last = msg->lines;
    while (last->next) last = last->next;
    last->next = new_line;
  }
  msg->line_count++;
}

static void render_tool_executions(message_t *msg) {
  if (!msg || !msg->tool_executions) return;

  tool_execution_t *exec = msg->tool_executions;
  while (exec) {
    char tool_header[256];
    snprintf(tool_header, sizeof(tool_header), "  ⚡ %s(", exec->tool_name);

    if (exec->parameters && strlen(exec->parameters) > 0) {
      cJSON *params_json = cJSON_Parse(exec->parameters);
      if (params_json) {
        char *params_str = cJSON_PrintUnformatted(params_json);

        if (params_str) {
          size_t params_len = strlen(params_str);
          if (params_len > 100) {
            params_str[97] = '.';
            params_str[98] = '.';
            params_str[99] = '.';
            params_str[100] = '\0';
          }

          strncat(tool_header, params_str,
                  sizeof(tool_header) - strlen(tool_header) - 1);
          free(params_str);
        }
        cJSON_Delete(params_json);
      } else {
        size_t remaining = sizeof(tool_header) - strlen(tool_header) - 1;
        if (strlen(exec->parameters) > remaining - 10) {
          strncat(tool_header, exec->parameters, remaining - 10);
          strcat(tool_header, "...");
        } else {
          strcat(tool_header, exec->parameters);
        }
      }
    }

    strcat(tool_header, ")");
    add_rendered_line(msg, tool_header, COLOR_LABEL_TOOL_EXEC | TB_BOLD);

    exec = exec->next;
  }
}

static void render_content_lines(message_t *msg, const char *content,
                                 uintattr_t color, int indent) {
  if (!content) return;

  if (is_json_content(content)) {
    char json_header[64];
    snprintf(json_header, sizeof(json_header), "%*sJSON Response:", indent, "");
    add_rendered_line(msg, json_header, COLOR_JSON_KEY | TB_BOLD);

    cJSON *json = cJSON_Parse(content);
    if (json) {
      char *formatted_json = cJSON_Print(json);
      if (formatted_json) {
        int json_width = app.chat_width - indent - 4;
        if (json_width < 20) json_width = 20;

        char **lines;
        int line_count;
        wrap_text_to_lines(formatted_json, json_width, &lines, &line_count);

        for (int i = 0; i < line_count; i++) {
          if (lines[i]) {
            size_t line_len = strlen(lines[i]) + indent + 1;
            char *indented_line = malloc(line_len);
            if (indented_line) {
              memset(indented_line, ' ', indent);
              strcpy(indented_line + indent, lines[i]);

              uintattr_t json_color = COLOR_JSON_STRING;
              if (strstr(lines[i], "{") || strstr(lines[i], "}") ||
                  strstr(lines[i], "[") || strstr(lines[i], "]"))
                json_color = COLOR_JSON_BRACE;
              else if (strstr(lines[i], ":"))
                json_color = COLOR_JSON_KEY;
              else if (strstr(lines[i], "true") || strstr(lines[i], "false"))
                json_color = COLOR_JSON_BOOLEAN;
              else if (strstr(lines[i], "null"))
                json_color = COLOR_JSON_NULL;

              add_rendered_line(msg, indented_line, json_color);
              free(indented_line);
            }
            free(lines[i]);
          }
        }
        if (lines) free(lines);
        free(formatted_json);
      }
      cJSON_Delete(json);
      return;
    }
  }

  int content_width = app.chat_width - indent - 4;
  if (content_width < 15) content_width = 15;

  int content_len = strlen(content);
  if (content_len > content_width * 3) {
    content_width = app.chat_width - indent - 2;
    if (content_width < 20) content_width = 20;
  }

  char **lines;
  int line_count;
  wrap_text_to_lines(content, content_width, &lines, &line_count);

  for (int i = 0; i < line_count; i++) {
    if (lines[i]) {
      size_t line_len = strlen(lines[i]) + indent + 1;
      char *indented_line = malloc(line_len);
      if (indented_line) {
        memset(indented_line, ' ', indent);
        strcpy(indented_line + indent, lines[i]);
        add_rendered_line(msg, indented_line, color);
        free(indented_line);
      }
      free(lines[i]);
    }
  }
  if (lines) free(lines);
}

static void render_message_content(message_t *msg) {
  if (!msg) return;

  if (msg->content && strlen(msg->content) > 0) {
    char *rendered_markdown = process_markdown_to_ansi(msg->content);
    if (rendered_markdown) {
      render_markdown_lines(msg, rendered_markdown);
      free(rendered_markdown);
      return;
    }
  }

  free_message_lines(msg);

  if (msg->type == MSG_ASSISTANT && msg->tool_executions) {
    render_tool_executions(msg);
    add_rendered_line(msg, "", COLOR_FG);
  }

  if (msg->content && strlen(msg->content) > 0) {
    render_content_lines(msg, msg->content, COLOR_FG, 2);
  }

  if (msg->is_streaming) {
    pthread_mutex_lock(&app.streaming.mutex);
    bool waiting = app.streaming.waiting_for_stream;
    bool active = app.streaming.active;
    pthread_mutex_unlock(&app.streaming.mutex);

    if (active) {
      if (waiting) {
        const char *frames[] = {"⠋", "⠙", "⠹", "⠸"};
        char thinking[32];
        snprintf(thinking, sizeof(thinking), "  %s Thinking...",
                 frames[app.animation.thinking_frame]);
        add_rendered_line(msg, thinking, COLOR_ACCENT | TB_BOLD);
      } else {
        if (app.animation.show_cursor) {
          add_rendered_line(msg, "  ▋", COLOR_ACCENT);
        }
      }
    }
  }

  msg->needs_rerender = false;
}

static void rebuild_all_message_rendering(void) {
  message_t *msg = app.messages;
  while (msg) {
    render_message_content(msg);
    msg = msg->next;
  }
  calculate_chat_metrics();
}

static void calculate_chat_metrics(void) {
  app.chat.total_lines = 0;

  message_t *msg = app.messages;
  while (msg) {
    app.chat.total_lines += msg->line_count;
    msg = msg->next;
  }

  app.chat.visible_lines = app.chat_height - 1;
  if (app.chat.visible_lines < 1) app.chat.visible_lines = 1;
}

static void scroll_chat(int lines) {
  if (lines == 0) return;

  app.chat.auto_scroll = false;

  app.chat.scroll_offset += lines;

  int max_scroll = app.chat.total_lines - app.chat.visible_lines;
  if (max_scroll < 0) max_scroll = 0;

  if (app.chat.scroll_offset < 0) app.chat.scroll_offset = 0;
  if (app.chat.scroll_offset > max_scroll) app.chat.scroll_offset = max_scroll;

  if (app.chat.scroll_offset == 0) {
    app.chat.auto_scroll = true;
  }
}

static void scroll_to_bottom(void) {
  app.chat.scroll_offset = 0;
  app.chat.auto_scroll = true;
}

static void scroll_to_top(void) {
  int max_scroll = app.chat.total_lines - app.chat.visible_lines;
  if (max_scroll < 0) max_scroll = 0;
  app.chat.scroll_offset = max_scroll;
  app.chat.auto_scroll = false;
}

typedef struct {
  uintattr_t fg_color;
  uintattr_t bg_color;
  bool bold;
  bool italic;
  bool underline;
  bool reverse;
  bool strikethrough;
  bool dim;
} ansi_state_t;

static uint32_t convert_8bit_color_to_rgb(int color_index) {
  static const uint32_t standard_colors[16] = {
      0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
      0x008080, 0xC0C0C0, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
      0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};

  if (color_index < 16) {
    return standard_colors[color_index];
  }

  if (color_index >= 16 && color_index <= 231) {
    int cube_index = color_index - 16;
    int r = (cube_index / 36) % 6;
    int g = (cube_index / 6) % 6;
    int b = cube_index % 6;

    r = r ? (r * 40 + 55) : 0;
    g = g ? (g * 40 + 55) : 0;
    b = b ? (b * 40 + 55) : 0;

    return (r << 16) | (g << 8) | b;
  }

  if (color_index >= 232 && color_index <= 255) {
    int gray = (color_index - 232) * 10 + 8;
    return (gray << 16) | (gray << 8) | gray;
  }

  return 0xFFFFFF;
}

static void parse_sgr_parameters(const char *params, ansi_state_t *state) {
  if (!params || !state) return;

  if (strlen(params) == 0) {
    state->fg_color = COLOR_FG;
    state->bg_color = COLOR_BG;
    state->bold = false;
    state->italic = false;
    state->underline = false;
    state->reverse = false;
    state->strikethrough = false;
    state->dim = false;
    return;
  }

  char *params_copy = strdup(params);
  if (!params_copy) return;

  char *saveptr;
  char *token = strtok_r(params_copy, ";", &saveptr);

  while (token != NULL) {
    int param = atoi(token);

    switch (param) {
      case 0:
        state->fg_color = COLOR_FG;
        state->bg_color = COLOR_BG;
        state->bold = false;
        state->italic = false;
        state->underline = false;
        state->reverse = false;
        state->strikethrough = false;
        state->dim = false;
        break;

      case 1:
        state->bold = true;
        break;
      case 2:
        state->dim = true;
        break;
      case 3:
        state->italic = true;
        break;
      case 4:
        state->underline = true;
        break;
      case 7:
        state->reverse = true;
        break;
      case 9:
        state->strikethrough = true;
        break;

      case 21:
      case 22:
        state->bold = false;
        state->dim = false;
        break;
      case 23:
        state->italic = false;
        break;
      case 24:
        state->underline = false;
        break;
      case 27:
        state->reverse = false;
        break;
      case 29:
        state->strikethrough = false;
        break;

      case 30:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x000000;
        break;
      case 31:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x800000;
        break;
      case 32:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x008000;
        break;
      case 33:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x808000;
        break;
      case 34:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x000080;
        break;
      case 35:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x800080;
        break;
      case 36:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x008080;
        break;
      case 37:
        state->fg_color = (state->fg_color & 0xFF000000) | 0xFFFFFF;
        break;
      case 39:
        state->fg_color =
            (state->fg_color & 0xFF000000) | (COLOR_FG & 0x00FFFFFF);
        break;

      case 40:
        state->bg_color = 0x000000;
        break;
      case 41:
        state->bg_color = 0x800000;
        break;
      case 42:
        state->bg_color = 0x008000;
        break;
      case 43:
        state->bg_color = 0x808000;
        break;
      case 44:
        state->bg_color = 0x000080;
        break;
      case 45:
        state->bg_color = 0x800080;
        break;
      case 46:
        state->bg_color = 0x008080;
        break;
      case 47:
        state->bg_color = 0xFFFFFF;
        break;
      case 49:
        state->bg_color = COLOR_BG;
        break;

      case 90:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x808080;
        break;
      case 91:
        state->fg_color = (state->fg_color & 0xFF000000) | 0xFF0000;
        break;
      case 92:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x00FF00;
        break;
      case 93:
        state->fg_color = (state->fg_color & 0xFF000000) | 0xFFFF00;
        break;
      case 94:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x0000FF;
        break;
      case 95:
        state->fg_color = (state->fg_color & 0xFF000000) | 0xFF00FF;
        break;
      case 96:
        state->fg_color = (state->fg_color & 0xFF000000) | 0x00FFFF;
        break;
      case 97:
        state->fg_color = (state->fg_color & 0xFF000000) | 0xFFFFFF;
        break;

      case 100:
        state->bg_color = 0x808080;
        break;
      case 101:
        state->bg_color = 0xFF0000;
        break;
      case 102:
        state->bg_color = 0x00FF00;
        break;
      case 103:
        state->bg_color = 0xFFFF00;
        break;
      case 104:
        state->bg_color = 0x0000FF;
        break;
      case 105:
        state->bg_color = 0xFF00FF;
        break;
      case 106:
        state->bg_color = 0x00FFFF;
        break;
      case 107:
        state->bg_color = 0xFFFFFF;
        break;

      case 38:
        token = strtok_r(NULL, ";", &saveptr);
        if (token != NULL) {
          int color_type = atoi(token);
          if (color_type == 5) {
            token = strtok_r(NULL, ";", &saveptr);
            if (token != NULL) {
              int color_index = atoi(token);
              uint32_t rgb = convert_8bit_color_to_rgb(color_index);
              state->fg_color = (state->fg_color & 0xFF000000) | rgb;
            }
          } else if (color_type == 2) {
            char *r_token = strtok_r(NULL, ";", &saveptr);
            char *g_token = strtok_r(NULL, ";", &saveptr);
            char *b_token = strtok_r(NULL, ";", &saveptr);
            if (r_token && g_token && b_token) {
              int r = atoi(r_token);
              int g = atoi(g_token);
              int b = atoi(b_token);
              state->fg_color = (state->fg_color & 0xFF000000) |
                                ((r & 0xFF) << 16) | ((g & 0xFF) << 8) |
                                (b & 0xFF);
            }
          }
        }
        break;

      case 48:
        token = strtok_r(NULL, ";", &saveptr);
        if (token != NULL) {
          int color_type = atoi(token);
          if (color_type == 5) {
            token = strtok_r(NULL, ";", &saveptr);
            if (token != NULL) {
              int color_index = atoi(token);
              state->bg_color = convert_8bit_color_to_rgb(color_index);
            }
          } else if (color_type == 2) {
            char *r_token = strtok_r(NULL, ";", &saveptr);
            char *g_token = strtok_r(NULL, ";", &saveptr);
            char *b_token = strtok_r(NULL, ";", &saveptr);
            if (r_token && g_token && b_token) {
              int r = atoi(r_token);
              int g = atoi(g_token);
              int b = atoi(b_token);
              state->bg_color =
                  ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            }
          }
        }
        break;
    }

    token = strtok_r(NULL, ";", &saveptr);
  }

  free(params_copy);
}

static void ansi_state_to_termbox(const ansi_state_t *state, uintattr_t *tb_fg,
                                  uintattr_t *tb_bg) {
  if (!state || !tb_fg || !tb_bg) return;

  *tb_fg = state->fg_color & 0x00FFFFFF;
  *tb_bg = state->bg_color & 0x00FFFFFF;

  if (state->bold) *tb_fg |= TB_BOLD;
  if (state->italic) *tb_fg |= TB_ITALIC;
  if (state->underline || state->strikethrough) *tb_fg |= TB_UNDERLINE;
  if (state->reverse) *tb_fg |= TB_REVERSE;

  if (state->dim && !state->bold) {
    uint32_t r = (*tb_fg >> 16) & 0xFF;
    uint32_t g = (*tb_fg >> 8) & 0xFF;
    uint32_t b = *tb_fg & 0xFF;

    r = (r * 2) / 3;
    g = (g * 2) / 3;
    b = (b * 2) / 3;

    *tb_fg = (*tb_fg & 0xFF000000) | (r << 16) | (g << 8) | b;
  }
}

static int render_chat_line_with_ansi(const char *text, int x, int y,
                                      int max_width, uintattr_t default_fg,
                                      uintattr_t default_bg) {
  if (!text || max_width <= 0) return 0;

  if (max_width < 5) max_width = 5;

  ansi_state_t state = {.fg_color = default_fg,
                        .bg_color = default_bg,
                        .bold = false,
                        .italic = false,
                        .underline = false,
                        .reverse = false,
                        .strikethrough = false,
                        .dim = false};

  const char *ptr = text;
  int current_x = x;
  int current_y = y;
  int line_start_x = x;
  int visible_chars_on_line = 0;

  int last_boundary_visible_chars = 0;
  bool found_word_boundary = false;

  bool in_escape = false;
  bool in_osc = false;
  char escape_buf[128];
  int escape_pos = 0;

  while (*ptr && current_y < tb_height()) {
    if (*ptr == '\033' && *(ptr + 1) == ']') {
      in_osc = true;
      ptr += 2;
      continue;
    }

    if (in_osc) {
      if (*ptr == '\033' && *(ptr + 1) == '\\') {
        in_osc = false;
        ptr += 2;
        continue;
      } else if (*ptr == '\007') {
        in_osc = false;
        ptr++;
        continue;
      } else {
        ptr++;
        continue;
      }
    }

    if (*ptr == '\033' && *(ptr + 1) == '[') {
      in_escape = true;
      escape_pos = 0;
      escape_buf[0] = '\0';
      ptr += 2;
      continue;
    }

    if (in_escape) {
      if ((*ptr >= '0' && *ptr <= '9') || *ptr == ';' || *ptr == ':') {
        if (escape_pos < sizeof(escape_buf) - 1) {
          escape_buf[escape_pos++] = *ptr;
          escape_buf[escape_pos] = '\0';
        }
        ptr++;
        continue;
      } else if (*ptr >= 0x20 && *ptr <= 0x2F) {
        if (escape_pos < sizeof(escape_buf) - 1) {
          escape_buf[escape_pos++] = *ptr;
          escape_buf[escape_pos] = '\0';
        }
        ptr++;
        continue;
      } else if (*ptr >= 0x40 && *ptr <= 0x7E) {
        in_escape = false;
        char final_byte = *ptr;

        if (final_byte == 'm') {
          parse_sgr_parameters(escape_buf, &state);
        }

        ptr++;
        continue;
      } else {
        in_escape = false;
        ptr++;
        continue;
      }
    }

    if (*ptr == '\033') {
      ptr++;
      if (*ptr) ptr++;
      continue;
    }

    if (*ptr == '\n') {
      current_y++;
      current_x = line_start_x;
      visible_chars_on_line = 0;
      ptr++;

      last_boundary_visible_chars = 0;
      found_word_boundary = false;
      continue;
    }

    uint32_t codepoint;
    int byte_len = tb_utf8_char_to_unicode(&codepoint, ptr);
    if (byte_len <= 0) {
      ptr++;
      continue;
    }

    bool is_boundary = is_word_boundary(codepoint);

    if (is_boundary && visible_chars_on_line > 0) {
      last_boundary_visible_chars = visible_chars_on_line + 1;
      found_word_boundary = true;
    }

    if (visible_chars_on_line >= max_width) {
      bool wrapped = false;

      if (found_word_boundary &&
          last_boundary_visible_chars < visible_chars_on_line &&
          last_boundary_visible_chars >= max_width / 4) {
        current_y++;
        current_x = line_start_x;
        visible_chars_on_line =
            visible_chars_on_line - last_boundary_visible_chars;

        if (is_boundary && (codepoint == ' ' || codepoint == '\t')) {
          ptr += byte_len;
          continue;
        }

        wrapped = true;
      } else {
        current_y++;
        current_x = line_start_x;
        visible_chars_on_line = 0;
        wrapped = true;
      }

      if (wrapped) {
        last_boundary_visible_chars = 0;
        found_word_boundary = false;
      }
    }

    uintattr_t tb_fg, tb_bg;
    ansi_state_to_termbox(&state, &tb_fg, &tb_bg);

    if (current_x >= 0 && current_x < tb_width() && current_y >= 0 &&
        current_y < tb_height()) {
      tb_set_cell(current_x, current_y, codepoint, tb_fg, tb_bg);
    }

    current_x++;
    visible_chars_on_line++;
    ptr += byte_len;
  }

  return current_y - y + 1;
}

static void render_message_header(message_t *msg, int x, int y, int max_width) {
  if (!msg) return;

  uintattr_t label_color = get_message_label_color(msg->type);
  const char *label_text = get_message_label_text(msg->type);
  const char *label_icon = get_message_label_icon(msg->type);

  char *time_str = format_time(msg->timestamp);
  char header_left[256];

  if (msg->tool_name) {
    snprintf(header_left, sizeof(header_left), "%s %s (%s)", label_icon,
             label_text, msg->tool_name);
  } else {
    snprintf(header_left, sizeof(header_left), "%s %s", label_icon, label_text);
  }

  int time_len = strlen(time_str);
  int header_len = strlen(header_left);

  if (max_width > header_len + time_len + 5) {
    char full_header[512];
    int padding = max_width - header_len - time_len;
    snprintf(full_header, sizeof(full_header), "%s%*s%s", header_left, padding,
             "", time_str);

    render_chat_line_with_ansi(full_header, x, y, max_width,
                               label_color | TB_BOLD, COLOR_BG);
  } else {
    render_chat_line_with_ansi(header_left, x, y, max_width,
                               label_color | TB_BOLD, COLOR_BG);

    char timestamp_line[64];
    snprintf(timestamp_line, sizeof(timestamp_line), "%*s%s",
             max_width - time_len, "", time_str);
    render_chat_line_with_ansi(timestamp_line, x, y + 1, max_width,
                               COLOR_TIMESTAMP, COLOR_BG);
  }

  free(time_str);
}

static void draw_chat_messages(void) {
  if (!app.messages) {
    int center_y = app.chat_height / 2;

    tb_printf(4, center_y - 1, COLOR_ASSISTANT | TB_BOLD, COLOR_BG,
              "Welcome to MOMO Chat with Apple Intelligence");

    tb_printf(4, center_y, COLOR_TIMESTAMP | TB_ITALIC, COLOR_BG,
              "Start typing your message below to begin a conversation...");

    tb_printf(4, center_y + 2, COLOR_TIMESTAMP, COLOR_BG,
              "────────────────────────────────────────");
    tb_printf(4, center_y + 3, COLOR_TIMESTAMP, COLOR_BG,
              "Tip: Use /help to see available commands");

    return;
  }

  typedef struct display_item {
    message_t *msg;
    bool is_header;
    bool is_separator;
    rendered_line_t *line;
    int header_lines;
  } display_item_t;

  int total_items = 0;
  message_t *msg = app.messages;
  while (msg) {
    total_items++;

    const char *label_text = get_message_label_text(msg->type);
    const char *label_icon = get_message_label_icon(msg->type);
    char *time_str = format_time(msg->timestamp);
    char header_left[256];

    if (msg->tool_name) {
      snprintf(header_left, sizeof(header_left), "%s %s (%s)", label_icon,
               label_text, msg->tool_name);
    } else {
      snprintf(header_left, sizeof(header_left), "%s %s", label_icon,
               label_text);
    }

    int available_width = app.chat_width - 4;
    int time_len = strlen(time_str);
    int header_len = strlen(header_left);

    if (available_width <= header_len + time_len + 5) {
      total_items++;
    }

    free(time_str);

    total_items += msg->line_count;

    total_items++;

    msg = msg->next;
  }

  if (total_items <= 0) {
    return;
  }

  display_item_t *all_items = malloc(sizeof(display_item_t) * total_items);
  if (!all_items) return;

  int item_index = 0;
  msg = app.messages;
  while (msg && item_index < total_items) {
    display_item_t *header_item = &all_items[item_index++];
    header_item->msg = msg;
    header_item->is_header = true;
    header_item->is_separator = false;
    header_item->line = NULL;

    const char *label_text = get_message_label_text(msg->type);
    const char *label_icon = get_message_label_icon(msg->type);
    char *time_str = format_time(msg->timestamp);
    char header_left[256];

    if (msg->tool_name) {
      snprintf(header_left, sizeof(header_left), "%s %s (%s)", label_icon,
               label_text, msg->tool_name);
    } else {
      snprintf(header_left, sizeof(header_left), "%s %s", label_icon,
               label_text);
    }

    int available_width = app.chat_width - 4;
    int time_len = strlen(time_str);
    int header_len = strlen(header_left);

    if (available_width > header_len + time_len + 5) {
      header_item->header_lines = 1;
    } else {
      header_item->header_lines = 2;
      if (item_index < total_items) {
        display_item_t *header_item2 = &all_items[item_index++];
        header_item2->msg = msg;
        header_item2->is_header = true;
        header_item2->is_separator = false;
        header_item2->line = NULL;
        header_item2->header_lines = 0;
      }
    }

    free(time_str);

    rendered_line_t *line = msg->lines;
    while (line && item_index < total_items) {
      display_item_t *content_item = &all_items[item_index++];
      content_item->msg = msg;
      content_item->is_header = false;
      content_item->is_separator = false;
      content_item->line = line;
      content_item->header_lines = 0;
      line = line->next;
    }

    if (item_index < total_items) {
      display_item_t *sep_item = &all_items[item_index++];
      sep_item->msg = msg;
      sep_item->is_header = false;
      sep_item->is_separator = true;
      sep_item->line = NULL;
      sep_item->header_lines = 0;
    }

    msg = msg->next;
  }

  int items_to_show = app.chat.visible_lines;
  if (items_to_show > item_index) items_to_show = item_index;

  int start_item = item_index - items_to_show - app.chat.scroll_offset;
  if (start_item < 0) start_item = 0;

  int end_item = start_item + items_to_show;
  if (end_item > item_index) end_item = item_index;

  int y = app.chat_height - 1;
  for (int i = end_item - 1; i >= start_item && y >= 1; i--) {
    if (i >= 0 && i < item_index) {
      display_item_t *item = &all_items[i];
      int x = 2;
      int max_width = app.chat_width - 4;

      if (max_width < 10) max_width = 10;

      if (item->is_header) {
        if (item->header_lines == 1) {
          render_message_header(item->msg, x, y, max_width);
        } else if (item->header_lines == 2) {
          render_message_header(item->msg, x, y - 1, max_width);
          y--;
        }
      } else if (item->is_separator) {
        int sep_width = max_width;
        if (sep_width > 200) sep_width = 200;
        if (sep_width < 1) sep_width = 1;

        char *separator = malloc(sep_width * 4 + 1);
        if (separator) {
          char utf8_dash[5];
          int dash_len = tb_utf8_unicode_to_char(utf8_dash, 0x2500);
          if (dash_len <= 0) {
            dash_len = 1;
            utf8_dash[0] = '-';
          }
          utf8_dash[dash_len] = '\0';

          separator[0] = '\0';
          for (int j = 0; j < sep_width; j++) {
            strcat(separator, utf8_dash);
          }

          render_chat_line_with_ansi(separator, x, y, max_width,
                                     COLOR_TIMESTAMP, COLOR_BG);
          free(separator);
        }
      } else if (item->line && item->line->text) {
        uintattr_t default_fg =
            (item->line->color == TB_DEFAULT) ? COLOR_FG : item->line->color;
        render_chat_line_with_ansi(item->line->text, x, y, max_width,
                                   default_fg, COLOR_BG);
      }
    }
    y--;
  }

  free(all_items);
}

static bool is_json_content(const char *content) {
  if (!content || strlen(content) == 0) return false;

  while (*content && (*content == ' ' || *content == '\t' || *content == '\n' ||
                      *content == '\r'))
    content++;

  return (*content == '{' || *content == '[');
}

static bool init_app_directory(void) {
  const char *home = getenv("HOME");
  if (!home) {
    show_error_message("HOME environment variable not set");
    return false;
  }

  size_t path_len = strlen(home) + strlen(APP_DIR_NAME) + 2;
  app.app_dir = malloc(path_len);
  if (!app.app_dir) {
    show_error_message("Failed to allocate memory for app directory");
    return false;
  }

  snprintf(app.app_dir, path_len, "%s/%s", home, APP_DIR_NAME);

  struct stat st = {0};
  if (stat(app.app_dir, &st) == -1) {
    if (mkdir(app.app_dir, 0755) != 0) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg),
               "Failed to create app directory: %s", app.app_dir);
      show_error_message(error_msg);
      return false;
    }
  }

  return true;
}

static bool load_tools_config(void) {
  if (!app.app_dir) {
    return false;
  }

  size_t path_len = strlen(app.app_dir) + strlen("/tools.json") + 1;
  char *tools_path = malloc(path_len);
  if (!tools_path) {
    return false;
  }
  snprintf(tools_path, path_len, "%s/tools.json", app.app_dir);

  FILE *file = fopen(tools_path, "r");
  if (!file) {
    free(tools_path);
    return true;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    fclose(file);
    free(tools_path);
    return true;
  }

  char *file_content = malloc(file_size + 1);
  if (!file_content) {
    fclose(file);
    free(tools_path);
    return false;
  }

  size_t read_size = fread(file_content, 1, file_size, file);
  file_content[read_size] = '\0';
  fclose(file);
  free(tools_path);

  cJSON *json = cJSON_Parse(file_content);
  if (!json) {
    free(file_content);
    show_error_message("Failed to parse tools.json - invalid JSON");
    return false;
  }

  if (!cJSON_IsArray(json)) {
    cJSON_Delete(json);
    free(file_content);
    show_error_message("tools.json must contain an array of tools");
    return false;
  }

  int array_size = cJSON_GetArraySize(json);
  app.tool_count = 0;

  for (int i = 0; i < array_size && app.tool_count < MAX_TOOLS; i++) {
    cJSON *tool_obj = cJSON_GetArrayItem(json, i);
    if (!cJSON_IsObject(tool_obj)) {
      continue;
    }

    cJSON *name = cJSON_GetObjectItem(tool_obj, "name");
    cJSON *description = cJSON_GetObjectItem(tool_obj, "description");
    cJSON *input_schema = cJSON_GetObjectItem(tool_obj, "input_schema");
    cJSON *mcp_obj = cJSON_GetObjectItem(tool_obj, "$mcp");

    if (!name || !cJSON_IsString(name)) {
      continue;
    }

    tool_config_t *tool = &app.tools[app.tool_count];
    memset(tool, 0, sizeof(tool_config_t));

    tool->name = strdup(cJSON_GetStringValue(name));

    if (description && cJSON_IsString(description)) {
      tool->description = strdup(cJSON_GetStringValue(description));
    }

    if (input_schema) {
      char *schema_str = cJSON_PrintUnformatted(input_schema);
      if (schema_str) {
        tool->input_schema = schema_str;
      }
    }

    if (mcp_obj && cJSON_IsObject(mcp_obj)) {
      tool->mcp = malloc(sizeof(mcp_config_t));
      if (tool->mcp) {
        memset(tool->mcp, 0, sizeof(mcp_config_t));

        cJSON *type = cJSON_GetObjectItem(mcp_obj, "type");
        if (type && cJSON_IsString(type)) {
          tool->mcp->type = strdup(cJSON_GetStringValue(type));
        }

        cJSON *command = cJSON_GetObjectItem(mcp_obj, "command");
        if (command && cJSON_IsString(command)) {
          tool->mcp->command = strdup(cJSON_GetStringValue(command));
        }

        cJSON *args = cJSON_GetObjectItem(mcp_obj, "args");
        if (args && cJSON_IsArray(args)) {
          int arg_count = cJSON_GetArraySize(args);
          tool->mcp->args = malloc(sizeof(char *) * (arg_count + 1));
          tool->mcp->arg_count = arg_count;

          for (int j = 0; j < arg_count; j++) {
            cJSON *arg = cJSON_GetArrayItem(args, j);
            if (arg && cJSON_IsString(arg)) {
              tool->mcp->args[j] = strdup(cJSON_GetStringValue(arg));
            } else {
              tool->mcp->args[j] = strdup("");
            }
          }
          tool->mcp->args[arg_count] = NULL;
        }

        cJSON *env = cJSON_GetObjectItem(mcp_obj, "env");
        if (env && cJSON_IsObject(env)) {
          int env_count = 0;
          cJSON *env_item = NULL;
          cJSON_ArrayForEach(env_item, env) { env_count++; }

          if (env_count > 0) {
            tool->mcp->env = malloc(sizeof(char *) * env_count);
            tool->mcp->env_count = env_count;

            int env_index = 0;
            cJSON_ArrayForEach(env_item, env) {
              if (cJSON_IsString(env_item)) {
                size_t env_str_len = strlen(env_item->string) +
                                     strlen(cJSON_GetStringValue(env_item)) + 2;
                char *env_str = malloc(env_str_len);
                snprintf(env_str, env_str_len, "%s=%s", env_item->string,
                         cJSON_GetStringValue(env_item));
                tool->mcp->env[env_index++] = env_str;
              }
            }
          }
        }

        tool->is_builtin = false;
      }
    } else {
      tool->is_builtin = true;
    }

    app.tool_count++;
  }

  cJSON_Delete(json);
  free(file_content);

  app.tools_json = create_tools_json_for_bridge();

  return true;
}

static char *create_tools_json_for_bridge(void) {
  cJSON *tools_array = cJSON_CreateArray();

  cJSON *time_tool = cJSON_CreateObject();
  cJSON_AddStringToObject(time_tool, "name", "get_current_time");
  cJSON_AddStringToObject(time_tool, "description",
                          "Get the current date and time");
  cJSON *time_schema = cJSON_CreateObject();
  cJSON_AddStringToObject(time_schema, "type", "object");
  cJSON_AddItemToObject(time_schema, "properties", cJSON_CreateObject());
  cJSON_AddItemToObject(time_schema, "required", cJSON_CreateArray());
  cJSON_AddItemToObject(time_tool, "input_schema", time_schema);
  cJSON_AddItemToArray(tools_array, time_tool);

  cJSON *calc_tool = cJSON_CreateObject();
  cJSON_AddStringToObject(calc_tool, "name", "calculate");
  cJSON_AddStringToObject(calc_tool, "description",
                          "Perform basic mathematical calculations");
  cJSON *calc_schema = cJSON_CreateObject();
  cJSON_AddStringToObject(calc_schema, "type", "object");
  cJSON *calc_props = cJSON_CreateObject();
  cJSON *expr_prop = cJSON_CreateObject();
  cJSON_AddStringToObject(expr_prop, "type", "string");
  cJSON_AddStringToObject(
      expr_prop, "description",
      "Mathematical expression to evaluate (e.g., '5 + 3', '10 * 2')");
  cJSON_AddItemToObject(calc_props, "expression", expr_prop);
  cJSON_AddItemToObject(calc_schema, "properties", calc_props);
  cJSON *calc_required = cJSON_CreateArray();
  cJSON_AddItemToArray(calc_required, cJSON_CreateString("expression"));
  cJSON_AddItemToObject(calc_schema, "required", calc_required);
  cJSON_AddItemToObject(calc_tool, "input_schema", calc_schema);
  cJSON_AddItemToArray(tools_array, calc_tool);

  cJSON *sys_tool = cJSON_CreateObject();
  cJSON_AddStringToObject(sys_tool, "name", "system_info");
  cJSON_AddStringToObject(sys_tool, "description",
                          "Get system and application information");
  cJSON *sys_schema = cJSON_CreateObject();
  cJSON_AddStringToObject(sys_schema, "type", "object");
  cJSON_AddItemToObject(sys_schema, "properties", cJSON_CreateObject());
  cJSON_AddItemToObject(sys_schema, "required", cJSON_CreateArray());
  cJSON_AddItemToObject(sys_tool, "input_schema", sys_schema);
  cJSON_AddItemToArray(tools_array, sys_tool);

  for (int i = 0; i < app.tool_count; i++) {
    tool_config_t *tool = &app.tools[i];

    cJSON *tool_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(tool_obj, "name", tool->name);

    if (tool->description) {
      cJSON_AddStringToObject(tool_obj, "description", tool->description);
    }

    if (tool->input_schema) {
      cJSON *schema = cJSON_Parse(tool->input_schema);
      if (schema) {
        cJSON_AddItemToObject(tool_obj, "input_schema", schema);
      }
    }

    cJSON_AddItemToArray(tools_array, tool_obj);
  }

  char *json_string = cJSON_Print(tools_array);
  cJSON_Delete(tools_array);

  return json_string;
}

static char *substitute_parameters(const char *template,
                                   const char *input_json) {
  if (!template || !input_json) {
    return strdup(template ? template : "");
  }

  cJSON *params = cJSON_Parse(input_json);
  if (!params) {
    return strdup(template);
  }

  size_t template_len = strlen(template);
  size_t result_size = template_len * 2;
  char *result = malloc(result_size);
  if (!result) {
    cJSON_Delete(params);
    return strdup(template);
  }

  size_t result_pos = 0;
  size_t i = 0;

  while (i < template_len) {
    if (i + 1 < template_len && template[i] == '{' && template[i + 1] == '{') {
      size_t start = i + 2;
      size_t end = start;

      while (end + 1 < template_len &&
             !(template[end] == '}' && template[end + 1] == '}')) {
        end++;
      }

      if (end + 1 < template_len) {
        size_t param_len = end - start;
        char *param_name = malloc(param_len + 1);
        if (param_name) {
          strncpy(param_name, template + start, param_len);
          param_name[param_len] = '\0';

          cJSON *param_value = cJSON_GetObjectItem(params, param_name);
          const char *replacement = "";
          if (param_value && cJSON_IsString(param_value)) {
            replacement = cJSON_GetStringValue(param_value);
          }

          size_t replacement_len = strlen(replacement);
          while (result_pos + replacement_len >= result_size) {
            result_size *= 2;
            char *new_result = realloc(result, result_size);
            if (!new_result) {
              free(param_name);
              free(result);
              cJSON_Delete(params);
              return strdup(template);
            }
            result = new_result;
          }

          strcpy(result + result_pos, replacement);
          result_pos += replacement_len;

          free(param_name);
        }

        i = end + 2;
      } else {
        result[result_pos++] = template[i++];
      }
    } else {
      if (result_pos >= result_size - 1) {
        result_size *= 2;
        char *new_result = realloc(result, result_size);
        if (!new_result) {
          free(result);
          cJSON_Delete(params);
          return strdup(template);
        }
        result = new_result;
      }
      result[result_pos++] = template[i++];
    }
  }

  result[result_pos] = '\0';
  cJSON_Delete(params);
  return result;
}

char *read_from_fd(int fd) {
  char *buffer = NULL;
  char temp_buf[4096];
  size_t total_size = 0;
  ssize_t bytes_read;

  while ((bytes_read = read(fd, temp_buf, sizeof(temp_buf) - 1)) > 0) {
    temp_buf[bytes_read] = '\0';

    char *new_buffer = realloc(buffer, total_size + bytes_read + 1);
    if (!new_buffer) {
      free(buffer);
      return NULL;
    }

    buffer = new_buffer;
    if (total_size == 0) {
      buffer[0] = '\0';
    }

    strcat(buffer, temp_buf);
    total_size += bytes_read;
  }

  if (!buffer) {
    buffer = malloc(1);
    if (buffer) buffer[0] = '\0';
  }

  return buffer;
}

static char *execute_mcp_tool(const mcp_config_t *mcp, const char *input_json) {
  if (!mcp || !mcp->command || strcmp(mcp->type, "stdio") != 0) {
    return strdup("{\"error\": \"Invalid MCP configuration\"}");
  }

  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[2];

  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 ||
      pipe(stderr_pipe) != 0) {
    return strdup("{\"error\": \"Failed to create pipes\"}");
  }

  char **argv = malloc(sizeof(char *) * (mcp->arg_count + 2));
  argv[0] = mcp->command;
  for (int i = 0; i < mcp->arg_count; i++) {
    argv[i + 1] = substitute_parameters(mcp->args[i], input_json);
  }
  argv[mcp->arg_count + 1] = NULL;

  pid_t pid = fork();
  if (pid == -1) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    for (int i = 1; i <= mcp->arg_count; i++) {
      free(argv[i]);
    }
    free(argv);
    return strdup("{\"error\": \"Failed to fork process\"}");
  }

  if (pid == 0) {
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    execvp(mcp->command, argv);

    fprintf(stderr, "Failed to execute %s: %s\n", mcp->command,
            strerror(errno));
    exit(EXIT_FAILURE);
  } else {
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (input_json && strlen(input_json) > 0) {
      ssize_t written = write(stdin_pipe[1], input_json, strlen(input_json));
      (void)written;
    }
    close(stdin_pipe[1]);

    char *stdout_data = read_from_fd(stdout_pipe[0]);
    char *stderr_data = read_from_fd(stderr_pipe[0]);

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);

    for (int i = 1; i <= mcp->arg_count; i++) {
      free(argv[i]);
    }
    free(argv);

    cJSON *response = cJSON_CreateObject();

    if (stdout_data && strlen(stdout_data) > 0) {
      cJSON_AddStringToObject(response, "stdout", stdout_data);
    }

    if (stderr_data && strlen(stderr_data) > 0) {
      cJSON_AddStringToObject(response, "stderr", stderr_data);
    }

    if (WIFEXITED(status)) {
      int exit_code = WEXITSTATUS(status);
      cJSON_AddNumberToObject(response, "exit_code", exit_code);
      if (exit_code != 0) {
        cJSON_AddStringToObject(response, "error", "Tool execution failed");
      }
    } else if (WIFSIGNALED(status)) {
      cJSON_AddStringToObject(response, "error", "Tool terminated by signal");
      cJSON_AddNumberToObject(response, "signal", WTERMSIG(status));
    }

    free(stdout_data);
    free(stderr_data);

    char *json_string = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    char *result = NULL;
    if (json_string) {
      char *sanitized = sanitize_utf8_string(json_string);
      free(json_string);
      if (sanitized) {
        result = sanitized;
      }
    }

    if (!result) {
      result = strdup("{\"error\": \"Failed to process tool output\"}");
    }

    return result;
  }
}

char *mcp_tool_handler(const char *parameters_json, void *user_data) {
  const char *tool_name = (const char *)user_data;

  tool_execution_t *exec_list = NULL;
  add_tool_execution_to_list(&exec_list, tool_name, parameters_json, NULL);

  pthread_mutex_lock(&app.streaming.mutex);
  message_t *current_msg = app.current_streaming;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (current_msg) {
    queue_message_update(current_msg, NULL, true, exec_list);
  }

  tool_config_t *tool = NULL;
  for (int i = 0; i < app.tool_count; i++) {
    if (app.tools[i].name && strcmp(app.tools[i].name, tool_name) == 0) {
      tool = &app.tools[i];
      break;
    }
  }

  if (!tool || !tool->mcp) {
    char *error_response =
        strdup("{\"error\": \"Tool not found or not an MCP tool\"}");

    if (current_msg) {
      tool_execution_t *updated_exec = NULL;
      add_tool_execution_to_list(&updated_exec, tool_name, parameters_json,
                                 error_response);
      queue_message_update(current_msg, NULL, true, updated_exec);
      free_tool_executions(updated_exec);
    }

    free_tool_executions(exec_list);
    return error_response;
  }

  char *result = execute_mcp_tool(tool->mcp, parameters_json);

  if (current_msg) {
    tool_execution_t *updated_exec = NULL;
    add_tool_execution_to_list(&updated_exec, tool_name, parameters_json,
                               result);
    queue_message_update(current_msg, NULL, true, updated_exec);
    free_tool_executions(updated_exec);
  }

  free_tool_executions(exec_list);
  return result;
}

static void free_mcp_config(mcp_config_t *mcp) {
  if (!mcp) return;

  if (mcp->type) free(mcp->type);
  if (mcp->command) free(mcp->command);

  if (mcp->args) {
    for (int i = 0; i < mcp->arg_count; i++) {
      if (mcp->args[i]) free(mcp->args[i]);
    }
    free(mcp->args);
  }

  if (mcp->env) {
    for (int i = 0; i < mcp->env_count; i++) {
      if (mcp->env[i]) free(mcp->env[i]);
    }
    free(mcp->env);
  }

  free(mcp);
}

static void free_tools_config(void) {
  for (int i = 0; i < app.tool_count; i++) {
    tool_config_t *tool = &app.tools[i];
    if (tool->name) free(tool->name);
    if (tool->description) free(tool->description);
    if (tool->input_schema) free(tool->input_schema);
    if (tool->mcp) free_mcp_config(tool->mcp);
  }
  app.tool_count = 0;

  if (app.tools_json) {
    free(app.tools_json);
    app.tools_json = NULL;
  }
}

char *tool_get_current_time(const char *parameters_json, void *user_data) {
  (void)user_data;

  tool_execution_t *exec_list = NULL;
  add_tool_execution_to_list(&exec_list, "get_current_time", parameters_json,
                             NULL);

  pthread_mutex_lock(&app.streaming.mutex);
  message_t *current_msg = app.current_streaming;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (current_msg) {
    queue_message_update(current_msg, NULL, true, exec_list);
  }

  time_t now = time(NULL);
  struct tm *local_time = localtime(&now);

  cJSON *response = cJSON_CreateObject();

  char time_str[64];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %Z", local_time);

  cJSON_AddStringToObject(response, "current_time", time_str);
  cJSON_AddNumberToObject(response, "timestamp", (double)now);

  char *json_string = cJSON_PrintUnformatted(response);
  cJSON_Delete(response);

  if (current_msg) {
    tool_execution_t *updated_exec = NULL;
    add_tool_execution_to_list(&updated_exec, "get_current_time",
                               parameters_json, json_string);
    queue_message_update(current_msg, NULL, true, updated_exec);
    free_tool_executions(updated_exec);
  }

  free_tool_executions(exec_list);
  return json_string;
}

char *tool_calculate(const char *parameters_json, void *user_data) {
  (void)user_data;

  tool_execution_t *exec_list = NULL;
  add_tool_execution_to_list(&exec_list, "calculate", parameters_json, NULL);

  pthread_mutex_lock(&app.streaming.mutex);
  message_t *current_msg = app.current_streaming;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (current_msg) {
    queue_message_update(current_msg, NULL, true, exec_list);
  }

  cJSON *params = cJSON_Parse(parameters_json);
  if (!params) {
    free_tool_executions(exec_list);
    return strdup("{\"error\": \"Invalid JSON parameters\"}");
  }

  cJSON *expression = cJSON_GetObjectItem(params, "expression");
  if (!expression || !cJSON_IsString(expression)) {
    cJSON_Delete(params);
    free_tool_executions(exec_list);
    return strdup("{\"error\": \"Missing 'expression' parameter\"}");
  }

  const char *expr = cJSON_GetStringValue(expression);
  double result = 0.0;
  bool valid = false;

  if (strstr(expr, "+")) {
    double a, b;
    if (sscanf(expr, "%lf + %lf", &a, &b) == 2) {
      result = a + b;
      valid = true;
    }
  } else if (strstr(expr, "-")) {
    double a, b;
    if (sscanf(expr, "%lf - %lf", &a, &b) == 2) {
      result = a - b;
      valid = true;
    }
  } else if (strstr(expr, "*")) {
    double a, b;
    if (sscanf(expr, "%lf * %lf", &a, &b) == 2) {
      result = a * b;
      valid = true;
    }
  } else if (strstr(expr, "/")) {
    double a, b;
    if (sscanf(expr, "%lf / %lf", &a, &b) == 2 && b != 0) {
      result = a / b;
      valid = true;
    }
  }

  cJSON *response = cJSON_CreateObject();

  if (valid) {
    cJSON_AddStringToObject(response, "expression", expr);
    cJSON_AddNumberToObject(response, "result", result);
  } else {
    cJSON_AddStringToObject(response, "error",
                            "Unsupported expression or invalid syntax");
  }

  char *json_string = cJSON_PrintUnformatted(response);
  cJSON_Delete(params);
  cJSON_Delete(response);

  if (current_msg) {
    tool_execution_t *updated_exec = NULL;
    add_tool_execution_to_list(&updated_exec, "calculate", parameters_json,
                               json_string);
    queue_message_update(current_msg, NULL, true, updated_exec);
    free_tool_executions(updated_exec);
  }

  free_tool_executions(exec_list);
  return json_string;
}

char *tool_system_info(const char *parameters_json, void *user_data) {
  (void)user_data;

  tool_execution_t *exec_list = NULL;
  add_tool_execution_to_list(&exec_list, "system_info", parameters_json, NULL);

  pthread_mutex_lock(&app.streaming.mutex);
  message_t *current_msg = app.current_streaming;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (current_msg) {
    queue_message_update(current_msg, NULL, true, exec_list);
  }

  cJSON *response = cJSON_CreateObject();

  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) == 0) {
    char *clean_hostname = sanitize_utf8_string(hostname);
    if (clean_hostname) {
      cJSON_AddStringToObject(response, "hostname", clean_hostname);
      free(clean_hostname);
    } else {
      cJSON_AddStringToObject(response, "hostname", "unknown");
    }
  }

  cJSON_AddNumberToObject(response, "terminal_width", app.term_width);
  cJSON_AddNumberToObject(response, "terminal_height", app.term_height);

  cJSON_AddNumberToObject(response, "current_fps", app.timing.current_fps);
  cJSON_AddNumberToObject(response, "smooth_fps", app.timing.smooth_fps);

  cJSON_AddStringToObject(response, "app_name", "MOMO CLI");
  cJSON_AddStringToObject(response, "app_version", "0.2.0");

  const char *intel_version = ai_get_version();
  char *clean_version = sanitize_utf8_string(intel_version);
  if (clean_version) {
    cJSON_AddStringToObject(response, "ai_version", clean_version);
    free(clean_version);
  } else {
    cJSON_AddStringToObject(response, "ai_version", "unknown");
  }

  char *json_string = cJSON_PrintUnformatted(response);
  cJSON_Delete(response);

  if (current_msg) {
    tool_execution_t *updated_exec = NULL;
    add_tool_execution_to_list(&updated_exec, "system_info", parameters_json,
                               json_string);
    queue_message_update(current_msg, NULL, true, updated_exec);
    free_tool_executions(updated_exec);
  }

  if (json_string) {
    char *final_clean = sanitize_utf8_string(json_string);
    free(json_string);

    free_tool_executions(exec_list);

    if (final_clean) {
      return final_clean;
    }
  }

  free_tool_executions(exec_list);
  return strdup("{\"error\":\"Failed to generate clean output\"}");
}

static void init_app(void) {
  setlocale(LC_CTYPE, "C.UTF-8");

  signal(SIGWINCH, handle_sigwinch);

  if (tb_init() != 0) {
    fprintf(stderr, "Failed to initialize termbox\n");
    exit(1);
  }

  if (tb_has_truecolor()) {
    tb_set_output_mode(TB_OUTPUT_TRUECOLOR);
  } else {
    tb_set_output_mode(TB_OUTPUT_256);
  }

  tb_set_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE | TB_INPUT_ALT);

  memset(&app, 0, sizeof(app));
  app.running = true;
  app.state = STATE_WELCOME;
  app.chat.auto_scroll = true;
  app.show_sidebar = true;
  app.input_height = INPUT_MIN_HEIGHT;
  app.chat.scroll_offset = 0;

  app.session_config.enable_tools = true;
  app.session_config.enable_history = true;
  app.session_config.enable_guardrails = true;
  app.session_config.response_mode = RESPONSE_MODE_TOOLS_ENABLED;
  app.session_config.temperature = 0.7;
  app.session_config.max_tokens = 2048;

  init_update_queue();
  pthread_mutex_init(&app.streaming.mutex, NULL);
  app.streaming.active = false;
  app.streaming.stream_id = AI_INVALID_ID;
  app.streaming.accumulated_text = NULL;
  app.streaming.accumulated_length = 0;
  app.streaming.waiting_for_stream = false;

  init_frame_timing();
  init_animations();

  update_dimensions();

  if (!init_app_directory()) {
    add_message(MSG_SYSTEM, "Failed to initialize application directory");
  }

  if (!load_tools_config()) {
    add_message(MSG_SYSTEM, "Failed to load tools configuration");
  }

  if (!init_ai_session()) {
    add_message(
        MSG_SYSTEM,
        "Apple Intelligence initialization failed. Check system requirements.");
  }
}

static bool init_ai_session(void) {
  ai_result_t result = ai_init();
  if (result != AI_SUCCESS) {
    show_error_with_code(result, "Failed to initialize libintelligence");
    return false;
  }

  app.ai_availability = ai_check_availability();
  app.availability_reason = ai_get_availability_reason();

  if (app.ai_availability != AI_AVAILABLE) {
    char avail_msg[256];
    snprintf(
        avail_msg, sizeof(avail_msg), "Apple Intelligence not available: %s",
        app.availability_reason ? app.availability_reason : "Unknown reason");
    add_message(MSG_SYSTEM, avail_msg);
    return false;
  }

  app.ai_context = ai_context_create();
  if (!app.ai_context) {
    show_error_message("Failed to create AI context");
    return false;
  }

  ai_session_config_t config = AI_DEFAULT_SESSION_CONFIG;
  config.enable_guardrails = app.session_config.enable_guardrails;
  config.prewarm = true;

  if (app.session_config.enable_tools && app.tools_json) {
    config.tools_json = app.tools_json;
  }

  app.ai_session = ai_create_session(app.ai_context, &config);
  if (app.ai_session == AI_INVALID_ID) {
    show_error_message("Failed to create AI session");
    return false;
  }

  if (app.session_config.enable_tools) {
    ai_result_t tool_result;

    tool_result =
        ai_register_tool(app.ai_context, app.ai_session, "get_current_time",
                         tool_get_current_time, NULL);
    if (tool_result != AI_SUCCESS) {
      show_error_with_code(tool_result,
                           "Failed to register get_current_time tool");
    }

    tool_result = ai_register_tool(app.ai_context, app.ai_session, "calculate",
                                   tool_calculate, NULL);
    if (tool_result != AI_SUCCESS) {
      show_error_with_code(tool_result, "Failed to register calculate tool");
    }

    tool_result = ai_register_tool(app.ai_context, app.ai_session,
                                   "system_info", tool_system_info, NULL);
    if (tool_result != AI_SUCCESS) {
      show_error_with_code(tool_result, "Failed to register system_info tool");
    }

    for (int i = 0; i < app.tool_count; i++) {
      tool_config_t *tool = &app.tools[i];
      if (tool->mcp && !tool->is_builtin) {
        tool_result =
            ai_register_tool(app.ai_context, app.ai_session, tool->name,
                             mcp_tool_handler, (void *)tool->name);
        if (tool_result != AI_SUCCESS) {
          char error_msg[256];
          snprintf(error_msg, sizeof(error_msg),
                   "Failed to register MCP tool: %s", tool->name);
          show_error_with_code(tool_result, error_msg);
        }
      }
    }
  }

  return true;
}

static void cleanup_ai_session(void) {
  if (app.ai_context && app.ai_session != AI_INVALID_ID) {
    ai_destroy_session(app.ai_context, app.ai_session);
  }

  if (app.ai_context) {
    ai_context_free(app.ai_context);
  }

  if (app.availability_reason) {
    ai_free_string(app.availability_reason);
  }

  ai_cleanup();
}

static void update_dimensions(void) {
  int new_width = tb_width();
  int new_height = tb_height();

  if (new_width < MIN_TERM_WIDTH || new_height < MIN_TERM_HEIGHT) {
    app.term_width = (new_width < MIN_TERM_WIDTH) ? MIN_TERM_WIDTH : new_width;
    app.term_height =
        (new_height < MIN_TERM_HEIGHT) ? MIN_TERM_HEIGHT : new_height;
  } else {
    app.term_width = new_width;
    app.term_height = new_height;
  }

  update_input_height();

  if (app.show_sidebar) {
    app.chat_width = app.term_width - SIDEBAR_WIDTH - 1;
    if (app.chat_width < MIN_CHAT_WIDTH) {
      app.chat_width = MIN_CHAT_WIDTH;
    }
    app.sidebar_x = app.chat_width + 1;
  } else {
    app.chat_width = app.term_width;
    app.sidebar_x = app.term_width;
  }

  app.chat_height = app.term_height - app.input_height - 1;

  rebuild_all_message_rendering();

  if (app.chat.auto_scroll) {
    scroll_to_bottom();
  } else {
    int max_scroll = app.chat.total_lines - app.chat.visible_lines;
    if (max_scroll < 0) max_scroll = 0;

    if (app.chat.scroll_offset > max_scroll)
      app.chat.scroll_offset = max_scroll;
  }
}

static void render_frame(void) {
  process_message_updates();

  message_t *msg = app.messages;
  while (msg) {
    if (msg->needs_rerender) {
      render_message_content(msg);
    }
    msg = msg->next;
  }

  calculate_chat_metrics();

  tb_clear();

  if (resize_pending) {
    resize_pending = 0;
    app.needs_resize = true;
  }

  if (app.needs_resize) {
    update_dimensions();
    app.needs_resize = false;
  }

  if (app.state == STATE_WELCOME) {
    draw_welcome_screen();
  } else {
    draw_chat_interface();
  }

  draw_input_bar();

  tb_present();
}

static bool pending_escape = false;

static void handle_input(struct tb_event *ev) {
  if (ev->type == TB_EVENT_MOUSE) {
    if (ev->key == TB_KEY_MOUSE_WHEEL_UP) {
      scroll_chat(3);
      return;
    } else if (ev->key == TB_KEY_MOUSE_WHEEL_DOWN) {
      scroll_chat(-3);
      return;
    }
    return;
  }

  pthread_mutex_lock(&app.streaming.mutex);
  bool streaming_active = app.streaming.active;
  ai_stream_id_t stream_id = app.streaming.stream_id;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (streaming_active && ev->key == TB_KEY_ESC) {
    if (stream_id != AI_INVALID_ID) {
      ai_result_t cancel_result = ai_cancel_stream(app.ai_context, stream_id);
      if (cancel_result != AI_SUCCESS) {
        show_error_with_code(cancel_result, "Failed to cancel stream");
      } else {
        add_message(MSG_SYSTEM, "Generation cancelled by user");
      }
    }

    pthread_mutex_lock(&app.streaming.mutex);
    app.streaming.active = false;
    app.streaming.stream_id = AI_INVALID_ID;
    app.streaming.waiting_for_stream = false;
    if (app.streaming.accumulated_text) {
      free(app.streaming.accumulated_text);
      app.streaming.accumulated_text = NULL;
    }
    app.streaming.accumulated_length = 0;
    if (app.current_streaming) {
      app.current_streaming->is_streaming = false;
      app.current_streaming->needs_rerender = true;
    }
    pthread_mutex_unlock(&app.streaming.mutex);

    return;
  }

  if (pending_escape && (ev->key == TB_KEY_CTRL_J || ev->key == TB_KEY_ENTER ||
                         ev->ch == 10 || ev->ch == 13)) {
    pending_escape = false;
    if (!streaming_active &&
        strlen(app.input_buffer) < MAX_MESSAGE_LENGTH - 1) {
      int current_len = strlen(app.input_buffer);
      memmove(app.input_buffer + app.input_pos + 1,
              app.input_buffer + app.input_pos,
              current_len - app.input_pos + 1);

      app.input_buffer[app.input_pos] = '\n';
      app.input_pos++;
      update_input_height();
    }
    return;
  }

  switch (ev->key) {
    case TB_KEY_CTRL_C:
      app.running = false;
      break;

    case TB_KEY_ESC:
      if (strlen(app.input_buffer) > 0) {
        pending_escape = true;
      } else {
        app.input_buffer[0] = '\0';
        app.input_pos = 0;
        app.input_scroll = 0;
        update_input_height();
        pending_escape = false;
      }
      break;

    case TB_KEY_CTRL_J:
      pending_escape = false;
      if (!streaming_active &&
          strlen(app.input_buffer) < MAX_MESSAGE_LENGTH - 1) {
        int current_len = strlen(app.input_buffer);
        memmove(app.input_buffer + app.input_pos + 1,
                app.input_buffer + app.input_pos,
                current_len - app.input_pos + 1);

        app.input_buffer[app.input_pos] = '\n';
        app.input_pos++;
        update_input_height();
      }
      break;

    case TB_KEY_ENTER:
      if (pending_escape) {
        app.input_buffer[0] = '\0';
        app.input_pos = 0;
        app.input_scroll = 0;
        update_input_height();
        pending_escape = false;
        break;
      }

      if (!streaming_active && strlen(app.input_buffer) > 0) {
        if (app.input_buffer[0] == '/') {
          process_command(app.input_buffer);
        } else {
          send_message(app.input_buffer);
        }
        app.input_buffer[0] = '\0';
        app.input_pos = 0;
        app.input_scroll = 0;
        update_input_height();
      }
      break;

    case TB_KEY_CTRL_H:
      pending_escape = false;
      if (app.input_pos > 0) {
        int word_start = app.input_pos;

        while (word_start > 0) {
          int prev_pos = word_start - 1;
          while (prev_pos > 0 && (app.input_buffer[prev_pos] & 0x80) &&
                 !(app.input_buffer[prev_pos] & 0x40)) {
            prev_pos--;
          }

          uint32_t codepoint;
          tb_utf8_char_to_unicode(&codepoint, app.input_buffer + prev_pos);

          if (!is_word_boundary(codepoint) && codepoint != '\n') break;

          word_start = prev_pos;
        }

        while (word_start > 0) {
          int prev_pos = word_start - 1;
          while (prev_pos > 0 && (app.input_buffer[prev_pos] & 0x80) &&
                 !(app.input_buffer[prev_pos] & 0x40)) {
            prev_pos--;
          }

          uint32_t codepoint;
          tb_utf8_char_to_unicode(&codepoint, app.input_buffer + prev_pos);

          if (is_word_boundary(codepoint) || codepoint == '\n') break;

          word_start = prev_pos;
        }

        if (word_start < app.input_pos) {
          memmove(app.input_buffer + word_start,
                  app.input_buffer + app.input_pos,
                  strlen(app.input_buffer) - app.input_pos + 1);

          app.input_pos = word_start;
          update_input_height();
        }
      }
      break;

    case TB_KEY_BACKSPACE2:
      pending_escape = false;
      if (app.input_pos > 0) {
        int char_start = app.input_pos - 1;

        while (char_start > 0 && (app.input_buffer[char_start] & 0x80) &&
               !(app.input_buffer[char_start] & 0x40)) {
          char_start--;
        }

        memmove(app.input_buffer + char_start, app.input_buffer + app.input_pos,
                strlen(app.input_buffer) - app.input_pos + 1);

        app.input_pos = char_start;
        update_input_height();
      }
      break;

    case TB_KEY_DELETE:
      pending_escape = false;
      if (app.input_pos < (int)strlen(app.input_buffer)) {
        if (ev->mod & TB_MOD_CTRL) {
          int word_end = app.input_pos;
          int input_len = strlen(app.input_buffer);

          while (word_end < input_len) {
            uint32_t codepoint;
            int byte_len = tb_utf8_char_to_unicode(&codepoint,
                                                   app.input_buffer + word_end);

            if (byte_len <= 0) {
              word_end++;
              continue;
            }

            if (!is_word_boundary(codepoint) && codepoint != '\n') break;

            word_end += byte_len;
          }

          while (word_end < input_len) {
            uint32_t codepoint;
            int byte_len = tb_utf8_char_to_unicode(&codepoint,
                                                   app.input_buffer + word_end);

            if (byte_len <= 0) {
              word_end++;
              continue;
            }

            if (is_word_boundary(codepoint) || codepoint == '\n') break;

            word_end += byte_len;
          }

          if (word_end > app.input_pos) {
            memmove(app.input_buffer + app.input_pos,
                    app.input_buffer + word_end,
                    strlen(app.input_buffer) - word_end + 1);

            update_input_height();
          }
        } else {
          uint32_t codepoint;
          int byte_len = tb_utf8_char_to_unicode(
              &codepoint, app.input_buffer + app.input_pos);

          if (byte_len <= 0) byte_len = 1;

          memmove(app.input_buffer + app.input_pos,
                  app.input_buffer + app.input_pos + byte_len,
                  strlen(app.input_buffer) - app.input_pos - byte_len + 1);

          update_input_height();
        }
      }
      break;

    case TB_KEY_ARROW_UP:
      pending_escape = false;
      if (ev->mod & TB_MOD_CTRL) {
        scroll_chat(1);
      } else {
        move_cursor_up_in_input();
      }
      break;

    case TB_KEY_ARROW_DOWN:
      pending_escape = false;
      if (ev->mod & TB_MOD_CTRL) {
        scroll_chat(-1);
      } else {
        move_cursor_down_in_input();
      }
      break;

    case TB_KEY_ARROW_LEFT:
      pending_escape = false;
      if (ev->mod & TB_MOD_ALT) {
        move_to_line_start();
      } else if (ev->mod & TB_MOD_SHIFT) {
        move_to_previous_word();
      } else {
        if (app.input_pos > 0) {
          app.input_pos--;

          while (app.input_pos > 0 &&
                 (app.input_buffer[app.input_pos] & 0x80) &&
                 !(app.input_buffer[app.input_pos] & 0x40)) {
            app.input_pos--;
          }
        }
      }
      break;

    case TB_KEY_ARROW_RIGHT:
      pending_escape = false;
      if (ev->mod & TB_MOD_ALT) {
        move_to_line_end();
      } else if (ev->mod & TB_MOD_SHIFT) {
        move_to_next_word();
      } else {
        if (app.input_pos < (int)strlen(app.input_buffer)) {
          uint32_t codepoint;
          int byte_len = tb_utf8_char_to_unicode(
              &codepoint, app.input_buffer + app.input_pos);

          if (byte_len > 0) {
            app.input_pos += byte_len;
          } else {
            app.input_pos++;
          }
        }
      }
      break;

    case TB_KEY_PGUP:
      pending_escape = false;
      scroll_chat(app.chat.visible_lines / 2);
      break;

    case TB_KEY_PGDN:
      pending_escape = false;
      scroll_chat(-(app.chat.visible_lines / 2));
      break;

    case TB_KEY_HOME:
      pending_escape = false;
      if (ev->mod & TB_MOD_CTRL) {
        app.input_pos = 0;
      } else if (ev->mod & TB_MOD_ALT) {
        scroll_to_top();
      } else {
        move_to_line_start();
      }
      break;

    case TB_KEY_END:
      pending_escape = false;
      if (ev->mod & TB_MOD_CTRL) {
        app.input_pos = strlen(app.input_buffer);
      } else if (ev->mod & TB_MOD_ALT) {
        scroll_to_bottom();
      } else {
        move_to_line_end();
      }
      break;

    case TB_KEY_CTRL_V:
      pending_escape = false;
      if (!streaming_active) {
        char *clipboard_text = get_clipboard_text();
        if (clipboard_text && strlen(clipboard_text) > 0) {
          int current_len = strlen(app.input_buffer);
          int clipboard_len = strlen(clipboard_text);
          int available_space = MAX_MESSAGE_LENGTH - 1 - current_len;

          if (available_space > 0) {
            int insert_len = (clipboard_len > available_space) ? available_space
                                                               : clipboard_len;
            char *sanitized_text = sanitize_utf8_string(clipboard_text);
            if (sanitized_text) {
              int sanitized_len = strlen(sanitized_text);
              insert_len = (sanitized_len > available_space) ? available_space
                                                             : sanitized_len;
              memmove(app.input_buffer + app.input_pos + insert_len,
                      app.input_buffer + app.input_pos,
                      current_len - app.input_pos + 1);

              memcpy(app.input_buffer + app.input_pos, sanitized_text,
                     insert_len);
              app.input_pos += insert_len;
              update_input_height();

              free(sanitized_text);
            }
          }
        }
        if (clipboard_text) {
          free(clipboard_text);
        }
      }
      break;

    case TB_KEY_F1:
      pending_escape = false;
      app.show_sidebar = !app.show_sidebar;
      update_dimensions();
      rebuild_all_message_rendering();
      if (app.chat.auto_scroll) {
        scroll_to_bottom();
      }
      break;

    default:
      if (ev->ch != 0 && ev->ch >= 32) {
        pending_escape = false;

        char utf8_bytes[5] = {0};
        int utf8_len = tb_utf8_unicode_to_char(utf8_bytes, ev->ch);

        if (utf8_len > 0) {
          int current_len = strlen(app.input_buffer);

          if (current_len + utf8_len < MAX_MESSAGE_LENGTH - 1) {
            memmove(app.input_buffer + app.input_pos + utf8_len,
                    app.input_buffer + app.input_pos,
                    current_len - app.input_pos + 1);

            for (int i = 0; i < utf8_len; i++) {
              app.input_buffer[app.input_pos + i] = utf8_bytes[i];
            }

            app.input_pos += utf8_len;
            update_input_height();
          }
        }
      }
      break;
  }
}

static int get_unicode_line_length(const uint32_t *line) {
  int len = 0;
  while (line[len] != 0x0000) {
    len++;
  }
  return len;
}

static void draw_logo_line(int x, int y, const uint32_t *line,
                           uintattr_t color) {
  int len = get_unicode_line_length(line);

  for (int i = 0; i < len; i++) {
    if (x + i < app.term_width) {
      tb_set_cell(x + i, y, line[i], color | TB_BOLD, COLOR_BG);
    }
  }
}

static void draw_welcome_screen(void) {
  int center_x = app.term_width / 2;
  int center_y = app.term_height / 2;

  int mo_width = get_unicode_line_length(momo_line1);
  int full_momo_width = mo_width * 2;
  int momo_start_x = center_x - full_momo_width / 2;
  int logo_start_y = center_y - 10;

  draw_logo_line(momo_start_x, logo_start_y, momo_line1, COLOR_LOGO_DARK);
  draw_logo_line(momo_start_x, logo_start_y + 1, momo_line2, COLOR_LOGO_DARK);
  draw_logo_line(momo_start_x, logo_start_y + 2, momo_line3, COLOR_LOGO_DARK);
  draw_logo_line(momo_start_x, logo_start_y + 3, momo_line4, COLOR_LOGO_DARK);
  draw_logo_line(momo_start_x, logo_start_y + 4, momo_line5, COLOR_LOGO_DARK);

  draw_logo_line(momo_start_x + mo_width, logo_start_y, momo_line1,
                 COLOR_LOGO_LIGHT);
  draw_logo_line(momo_start_x + mo_width, logo_start_y + 1, momo_line2,
                 COLOR_LOGO_LIGHT);
  draw_logo_line(momo_start_x + mo_width, logo_start_y + 2, momo_line3,
                 COLOR_LOGO_LIGHT);
  draw_logo_line(momo_start_x + mo_width, logo_start_y + 3, momo_line4,
                 COLOR_LOGO_LIGHT);
  draw_logo_line(momo_start_x + mo_width, logo_start_y + 4, momo_line5,
                 COLOR_LOGO_LIGHT);

  tb_printf(center_x + full_momo_width / 2 - 8, logo_start_y + 6,
            COLOR_ACCENT | TB_BOLD, COLOR_BG, "v0.2.0");

  int cmd_y = center_y - 3;
  tb_printf(center_x - 18, cmd_y++, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG,
            "COMMANDS");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG,
            "/help      show commands");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG,
            "/new       new session");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG,
            "/tools     toggle tools");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG,
            "/status    system status");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG,
            "/clear     clear chat");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG, "⌘+⏎       new line");
  tb_printf(center_x - 15, cmd_y++, COLOR_FG, COLOR_BG,
            "F1         toggle sidebar");

  if (app.term_width > 30) {
    uintattr_t status_color =
        (app.ai_availability == AI_AVAILABLE) ? COLOR_SUCCESS : COLOR_ERROR;
    const char *status_text = (app.ai_availability == AI_AVAILABLE)
                                  ? "◆ APPLE INTELLIGENCE READY"
                                  : "● APPLE INTELLIGENCE UNAVAILABLE";

    tb_printf(app.term_width - 35, app.term_height - app.input_height - 2,
              status_color | TB_BOLD, COLOR_BG, "%s", status_text);
  }

  tb_printf(2, 1, COLOR_TIMESTAMP | TB_BOLD, COLOR_BG, "⚡ %.1f FPS",
            app.timing.smooth_fps);
}

static void draw_chat_interface(void) {
  draw_chat_messages();

  if (app.show_sidebar) {
    draw_sidebar();

    int separator_x = app.chat_width;
    if (separator_x > 0 && separator_x < app.term_width) {
      for (int y = 0; y < app.term_height - app.input_height; y++) {
        tb_set_cell(separator_x, y, 0x2502, COLOR_DIM, COLOR_BG);
      }
    }
  }
}

static void draw_sidebar(void) {
  int x = app.sidebar_x;
  int y = 1;

  if (x >= app.term_width - 5) return;

  tb_printf(x, y++, COLOR_ACCENT | TB_BOLD, COLOR_BG, "◆ SYSTEM INFO");
  y++;

  tb_printf(x, y++, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG, "▶ AI STATUS");
  if (app.ai_availability == AI_AVAILABLE) {
    tb_printf(x + 2, y++, COLOR_SUCCESS | TB_BOLD, COLOR_BG, "● Available");
  } else {
    tb_printf(x + 2, y++, COLOR_ERROR | TB_BOLD, COLOR_BG, "● Unavailable");
  }

  pthread_mutex_lock(&app.streaming.mutex);
  bool streaming_active = app.streaming.active;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (streaming_active) {
    tb_printf(x + 2, y++, COLOR_ACCENT | TB_BOLD, COLOR_BG, "⚡ Generating...");
  } else {
    tb_printf(x + 2, y++, COLOR_SUCCESS, COLOR_BG, "◆ Ready");
  }
  y++;

  tb_printf(x, y++, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG, "▶ SESSION");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Tools: %s",
            app.session_config.enable_tools ? "ON" : "OFF");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Temp: %.1f",
            app.session_config.temperature);
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Tokens: %d",
            app.session_config.max_tokens);
  if (app.tool_count > 0) {
    tb_printf(x + 2, y++, COLOR_LABEL_TOOL_EXEC, COLOR_BG, "⚡ MCP Tools: %d",
              app.tool_count);
  }
  y++;

  tb_printf(x, y++, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG, "▶ DISPLAY");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Flow: Bottom-up");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Markdown: ON");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Input: Dynamic");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Sidebar: %s",
            app.show_sidebar ? "ON" : "OFF");
  y++;

  update_app_stats();
  tb_printf(x, y++, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG, "▶ STATISTICS");
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Messages: %d", app.message_count);
  tb_printf(x + 2, y++, COLOR_FG, COLOR_BG, "Lines: %d", app.chat.total_lines);
  tb_printf(x + 2, y++, COLOR_ACCENT, COLOR_BG, "FPS: %.1f",
            app.timing.smooth_fps);
  y++;

  tb_printf(x, y++, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG, "▶ CONTROLS");
  tb_printf(x + 2, y++, COLOR_TIMESTAMP, COLOR_BG, "↑↓←→ Navigate");
  tb_printf(x + 2, y++, COLOR_TIMESTAMP, COLOR_BG, "⌘+↑↓ Scroll chat");
  tb_printf(x + 2, y++, COLOR_TIMESTAMP, COLOR_BG, "⌘+⏎ New line");
  tb_printf(x + 2, y++, COLOR_TIMESTAMP, COLOR_BG, "F1 Toggle sidebar");
  tb_printf(x + 2, y++, COLOR_TIMESTAMP, COLOR_BG, "Ctrl+C Exit");
}

static void draw_input_bar(void) {
  int input_y = app.term_height - app.input_height;

  for (int x = 0; x < app.term_width; x++) {
    tb_set_cell(x, input_y, 0x2500, COLOR_ACCENT, COLOR_BG);
  }

  tb_printf(2, input_y + 1, COLOR_ACCENT | TB_BOLD, COLOR_BG, "▶");

  pthread_mutex_lock(&app.streaming.mutex);
  bool streaming_active = app.streaming.active;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (streaming_active) {
    const char *loading_frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧"};
    char status_text[64];
    snprintf(status_text, sizeof(status_text), "%s Generating response...",
             loading_frames[app.animation.loading_dots_frame % 8]);

    tb_printf(4, input_y + 1, COLOR_LABEL_ASSISTANT | TB_BOLD, COLOR_BG, "%s",
              status_text);
    tb_printf(4, input_y + 2, COLOR_TIMESTAMP | TB_ITALIC, COLOR_BG,
              "Press ESC to cancel generation");
  } else {
    int display_width = app.term_width - 8;
    if (display_width < 20) display_width = 20;

    int cursor_x = 4;
    int cursor_y = input_y + 1;

    if (strlen(app.input_buffer) > 0) {
      int chars_to_cursor = 0;
      int byte_pos = 0;

      while (byte_pos < app.input_pos && app.input_buffer[byte_pos]) {
        if (app.input_buffer[byte_pos] == '\n') {
          chars_to_cursor = 0;
          cursor_y++;
          cursor_x = 4;
          byte_pos++;
          continue;
        }

        uint32_t codepoint;
        int byte_len =
            tb_utf8_char_to_unicode(&codepoint, app.input_buffer + byte_pos);
        if (byte_len <= 0) {
          byte_pos++;
          continue;
        }

        if (chars_to_cursor >= display_width) {
          chars_to_cursor = 0;
          cursor_y++;
          cursor_x = 4;

          if (cursor_y < app.term_height) {
            tb_set_cell(app.term_width - 2, cursor_y - 1, 0x21B5, COLOR_ACCENT,
                        COLOR_BG);
          }
        }

        chars_to_cursor++;
        cursor_x++;
        byte_pos += byte_len;
      }

      if (chars_to_cursor >= display_width) {
        cursor_y++;
        cursor_x = 4;
      }
    }

    if (strlen(app.input_buffer) > 0) {
      const char *ptr = app.input_buffer;
      int current_x = 4;
      int current_y = input_y + 1;
      int line_char_count = 0;

      while (*ptr && current_y < app.term_height) {
        if (*ptr == '\n') {
          if (current_x < app.term_width - 1) {
            tb_set_cell(current_x, current_y, 0x23CE, COLOR_ACCENT, COLOR_BG);
          }

          current_y++;
          current_x = 4;
          line_char_count = 0;
          ptr++;
          continue;
        }

        uint32_t codepoint;
        int byte_len = tb_utf8_char_to_unicode(&codepoint, ptr);
        if (byte_len <= 0) {
          ptr++;
          continue;
        }

        if (line_char_count >= display_width) {
          if (current_x > 4) {
            tb_set_cell(app.term_width - 2, current_y, 0x21B5, COLOR_ACCENT,
                        COLOR_BG);
          }

          current_y++;
          current_x = 4;
          line_char_count = 0;

          if (current_y >= app.term_height) break;
        }

        uintattr_t char_color = COLOR_FG;

        if (codepoint == '/' && current_x == 4) {
          char_color = COLOR_LABEL_SYSTEM | TB_BOLD;
        } else if (codepoint == '@' || codepoint == '#') {
          char_color = COLOR_ACCENT;
        } else if (codepoint >= '0' && codepoint <= '9') {
          char_color = COLOR_JSON_NUMBER;
        } else if (codepoint == '"' || codepoint == '\'') {
          char_color = COLOR_JSON_STRING;
        }

        if (current_x < app.term_width - 1) {
          tb_set_cell(current_x, current_y, codepoint, char_color, COLOR_BG);
        }

        current_x++;
        line_char_count++;
        ptr += byte_len;
      }
    }

    if (cursor_x >= 4 && cursor_x < app.term_width - 1 &&
        cursor_y >= input_y + 1 && cursor_y < app.term_height) {
      if (app.animation.show_cursor) {
        uint32_t cursor_char = 0x2588;
        uintattr_t cursor_color = COLOR_ACCENT | TB_BOLD;

        if (app.session_config.enable_tools) {
          cursor_char = 0x258C;
          cursor_color = COLOR_LABEL_ASSISTANT | TB_BOLD;
        }

        tb_set_cell(cursor_x, cursor_y, cursor_char, cursor_color, COLOR_BG);
      }

      tb_set_cursor(cursor_x, cursor_y);
    }

    int status_y = app.term_height - 1;

    const char *mode = app.session_config.enable_tools ? "TOOLS" : "NORMAL";
    const char *sidebar_mode = app.show_sidebar ? "SIDEBAR" : "WIDE";
    tb_printf(2, status_y, COLOR_LABEL_SYSTEM | TB_BOLD, COLOR_BG, "▶ %s",
              mode);
    tb_printf(15, status_y, COLOR_TIMESTAMP, COLOR_BG, "View: %s",
              sidebar_mode);

    if (app.term_width > 80) {
      int char_count = strlen(app.input_buffer);
      int center_x = app.term_width / 2 - 10;
      if (center_x > 30) {
        tb_printf(center_x, status_y, COLOR_TIMESTAMP, COLOR_BG, "Chars: %d",
                  char_count);

        if (char_count > 0) {
          int lines = calculate_input_lines();
          tb_printf(center_x + 12, status_y, COLOR_TIMESTAMP, COLOR_BG,
                    "Lines: %d", lines);
        }
      }
    }

    if (app.term_width > 60) {
      char fps_text[32];
      snprintf(fps_text, sizeof(fps_text), "⚡ %.1f FPS",
               app.timing.smooth_fps);
      int fps_x = app.term_width - strlen(fps_text) - 2;

      if (fps_x > app.term_width / 2) {
        tb_printf(fps_x, status_y, COLOR_ACCENT, COLOR_BG, "%s", fps_text);
      }
    }

    if (app.term_width > 100) {
      const char *ai_status =
          (app.ai_availability == AI_AVAILABLE) ? "◆ AI:OK" : "● AI:--";
      tb_printf(
          app.term_width - 10, status_y,
          (app.ai_availability == AI_AVAILABLE) ? COLOR_SUCCESS : COLOR_ERROR,
          COLOR_BG, "%s", ai_status);
    }

    if (app.chat.total_lines > app.chat.visible_lines && app.term_width > 120) {
      int max_scroll = app.chat.total_lines - app.chat.visible_lines;
      if (max_scroll > 0) {
        float scroll_pct = (float)app.chat.scroll_offset / max_scroll;
        if (scroll_pct > 1.0f) scroll_pct = 1.0f;
        if (scroll_pct < 0.0f) scroll_pct = 0.0f;

        char scroll_text[16];
        snprintf(scroll_text, sizeof(scroll_text), "%3.0f%%", scroll_pct * 100);
        tb_printf(app.term_width - 22, status_y, COLOR_TIMESTAMP, COLOR_BG,
                  "%s", scroll_text);
      }
    }

    if (strlen(app.input_buffer) == 0 && !streaming_active) {
      tb_printf(4, input_y + 1, COLOR_TIMESTAMP | TB_ITALIC, COLOR_BG,
                "Type a message or /help for commands...");
    }
  }
}

static void process_command(const char *input) {
  if (strcmp(input, "/help") == 0) {
    add_message(MSG_SYSTEM,
                "◆ MOMO CLI COMMANDS\n\n"
                "▶ Basic Commands:\n"
                "/help - Show this help message\n"
                "/new - Start new session\n"
                "/clear - Clear chat history\n"
                "/tools - Toggle tools on/off\n"
                "/status - Show system status\n"
                "/sidebar - Toggle sidebar\n"
                "/exit - Exit application\n\n"
                "▶ Configuration:\n"
                "/temp <value> - Set temperature (0.0-2.0)\n"
                "/tokens <value> - Set max tokens (1-65536)\n"
                "/schema {filepath} - Use structured schema\n\n"
                "▶ Multi-line Input:\n"
                "⏎ Enter - Send message\n"
                "⌘+⏎ Cmd+Enter - New line (or ⌥+⏎ Alt+Enter)\n"
                "↑↓ - Move cursor up/down in input\n"
                "⌘+↑↓ - Scroll chat up/down\n\n"
                "▶ Navigation:\n"
                "←→ - Move character by character\n"
                "⇧+←→ - Jump by words\n"
                "⌥+←→ - Jump to start/end of line\n"
                "⌘+Home/End - Start/end of input\n"
                "⌥+Home/End - Top/bottom of chat\n"
                "Page ↑↓ - Scroll chat by half screen\n"
                "F1 - Toggle sidebar");
  } else if (strcmp(input, "/clear") == 0) {
    ai_result_t result =
        ai_clear_session_history(app.ai_context, app.ai_session);
    if (result == AI_SUCCESS) {
      free_messages();
      calculate_chat_metrics();
      scroll_to_bottom();
      add_message(MSG_SYSTEM, "◆ Chat history cleared successfully");
    } else {
      show_error_with_code(result, "Failed to clear session history");
    }
  } else if (strcmp(input, "/new") == 0) {
    free_messages();
    cleanup_ai_session();
    if (init_ai_session()) {
      app.state = STATE_CHAT;
      calculate_chat_metrics();
      scroll_to_bottom();
      add_message(MSG_SYSTEM, "◆ New session started successfully");
    } else {
      add_message(MSG_SYSTEM, "● Failed to start new session");
    }
  } else if (strcmp(input, "/tools") == 0) {
    app.session_config.enable_tools = !app.session_config.enable_tools;
    char msg[64];
    snprintf(msg, sizeof(msg), "⚡ Tools %s (restart with /new to apply)",
             app.session_config.enable_tools ? "enabled" : "disabled");
    add_message(MSG_SYSTEM, msg);
  } else if (strcmp(input, "/sidebar") == 0) {
    app.show_sidebar = !app.show_sidebar;
    update_dimensions();
    rebuild_all_message_rendering();
    if (app.chat.auto_scroll) {
      scroll_to_bottom();
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "▶ Sidebar %s",
             app.show_sidebar ? "enabled" : "disabled");
    add_message(MSG_SYSTEM, msg);
  } else if (strcmp(input, "/status") == 0) {
    const char *avail_desc;
    const char *avail_icon;
    switch (app.ai_availability) {
      case AI_AVAILABLE:
        avail_desc = "Available";
        avail_icon = "◆";
        break;
      case AI_DEVICE_NOT_ELIGIBLE:
        avail_desc = "Device not eligible";
        avail_icon = "●";
        break;
      case AI_NOT_ENABLED:
        avail_desc = "Not enabled";
        avail_icon = "●";
        break;
      case AI_MODEL_NOT_READY:
        avail_desc = "Model downloading";
        avail_icon = "⚡";
        break;
      default:
        avail_desc = "Unknown";
        avail_icon = "?";
        break;
    }

    char status_msg[512];
    snprintf(status_msg, sizeof(status_msg),
             "◆ SYSTEM STATUS\n\n"
             "▶ AI: %s %s\n"
             "▶ Tools: %s\n"
             "▶ Temperature: %.1f\n"
             "▶ Max Tokens: %d\n"
             "▶ MCP Tools: %d\n"
             "▶ Thread Safety: ENABLED\n"
             "▶ Performance: %.1f FPS",
             avail_icon, avail_desc,
             app.session_config.enable_tools ? "⚡ ENABLED" : "● DISABLED",
             app.session_config.temperature, app.session_config.max_tokens,
             app.tool_count, app.timing.smooth_fps);

    add_message(MSG_SYSTEM, status_msg);
  } else if (strncmp(input, "/temp ", 6) == 0) {
    float temp = atof(input + 6);
    if (temp >= 0.0 && temp <= 2.0) {
      app.session_config.temperature = temp;
      char msg[32];
      snprintf(msg, sizeof(msg), "▶ Temperature set to %.1f", temp);
      add_message(MSG_SYSTEM, msg);
    } else {
      add_message(MSG_SYSTEM, "● Temperature must be 0.0-2.0");
    }
  } else if (strncmp(input, "/tokens ", 8) == 0) {
    int tokens = atoi(input + 8);
    if (tokens >= 1 && tokens <= 65536) {
      app.session_config.max_tokens = tokens;
      char msg[32];
      snprintf(msg, sizeof(msg), "▶ Max tokens set to %d", tokens);
      add_message(MSG_SYSTEM, msg);
    } else {
      add_message(MSG_SYSTEM, "● Tokens must be 1-65536");
    }
  } else if (strcmp(input, "/exit") == 0) {
    app.running = false;
  } else {
    char msg[128];
    snprintf(msg, sizeof(msg), "● Unknown command: %s", input);
    add_message(MSG_SYSTEM, msg);
  }

  if (app.state == STATE_WELCOME) {
    app.state = STATE_CHAT;
  }
}

static void streaming_callback(ai_context_t *context, const char *chunk,
                               void *user_data) {
  (void)context;
  (void)user_data;

  pthread_mutex_lock(&app.streaming.mutex);

  if (chunk == NULL) {
    app.streaming.active = false;
    app.streaming.stream_id = AI_INVALID_ID;
    app.streaming.waiting_for_stream = false;

    if (app.current_streaming && app.streaming.accumulated_text) {
      queue_message_update(app.current_streaming,
                           app.streaming.accumulated_text, false, NULL);
    }

    if (app.streaming.accumulated_text) {
      free(app.streaming.accumulated_text);
      app.streaming.accumulated_text = NULL;
    }
    app.streaming.accumulated_length = 0;
  } else {
    if (app.streaming.waiting_for_stream) {
      app.streaming.waiting_for_stream = false;
    }

    size_t chunk_len = strlen(chunk);
    size_t old_len = app.streaming.accumulated_length;
    size_t new_len = old_len + chunk_len;

    char *new_text = realloc(app.streaming.accumulated_text, new_len + 1);
    if (new_text) {
      app.streaming.accumulated_text = new_text;

      memcpy(app.streaming.accumulated_text + old_len, chunk, chunk_len);
      app.streaming.accumulated_length = new_len;

      app.streaming.accumulated_text[new_len] = '\0';

      if (app.current_streaming) {
        queue_message_update(app.current_streaming,
                             app.streaming.accumulated_text, true, NULL);
      }
    }
  }

  pthread_mutex_unlock(&app.streaming.mutex);
}

static void send_message(const char *message) {
  if (app.state == STATE_WELCOME) {
    app.state = STATE_CHAT;
  }

  if (app.ai_availability != AI_AVAILABLE) {
    add_message(MSG_USER, message);
    add_message(MSG_SYSTEM, "● Apple Intelligence is not available");
    return;
  }

  char *extracted_message = NULL;
  char *schema_content = NULL;
  bool has_schema =
      parse_schema_directive(message, &extracted_message, &schema_content);

  if (has_schema) {
    if (schema_content) {
      char user_msg[512];
      snprintf(user_msg, sizeof(user_msg), "%s\n⚡ [Using structured schema]",
               extracted_message);
      add_message(MSG_USER, user_msg);

      start_streaming_response(extracted_message, schema_content);

      free(extracted_message);
      free(schema_content);
    } else {
      add_message(MSG_USER, message);
      add_message(MSG_SYSTEM, "● Failed to load schema file");
    }
  } else {
    add_message(MSG_USER, message);
    start_streaming_response(message, NULL);
  }
}

static void start_streaming_response(const char *prompt, const char *schema) {
  pthread_mutex_lock(&app.streaming.mutex);
  app.streaming.active = true;
  app.streaming.stream_id = AI_INVALID_ID;
  app.streaming.waiting_for_stream = true;
  if (app.streaming.accumulated_text) {
    free(app.streaming.accumulated_text);
  }
  app.streaming.accumulated_text = malloc(1);
  app.streaming.accumulated_text[0] = '\0';
  app.streaming.accumulated_length = 0;
  pthread_mutex_unlock(&app.streaming.mutex);

  add_message(MSG_ASSISTANT, "");

  message_t *msg = app.messages;
  while (msg && msg->next) {
    msg = msg->next;
  }

  pthread_mutex_lock(&app.streaming.mutex);
  app.current_streaming = msg;
  pthread_mutex_unlock(&app.streaming.mutex);

  if (msg) {
    msg->is_streaming = true;
    msg->needs_rerender = true;
  }

  ai_generation_params_t params = AI_DEFAULT_PARAMS;
  params.temperature = app.session_config.temperature;
  params.max_tokens = app.session_config.max_tokens;

  ai_stream_id_t stream_id;

  if (schema) {
    stream_id = ai_generate_structured_response_stream(
        app.ai_context, app.ai_session, prompt, schema, &params,
        streaming_callback, NULL);
  } else {
    stream_id =
        ai_generate_response_stream(app.ai_context, app.ai_session, prompt,
                                    &params, streaming_callback, NULL);
  }

  if (stream_id == AI_INVALID_ID) {
    pthread_mutex_lock(&app.streaming.mutex);
    app.streaming.active = false;
    app.streaming.waiting_for_stream = false;
    if (app.current_streaming) {
      queue_message_update(app.current_streaming,
                           "● Error: Failed to start generation", false, NULL);
    }
    pthread_mutex_unlock(&app.streaming.mutex);

    const char *ai_error = ai_get_last_error(app.ai_context);
    if (ai_error && strlen(ai_error) > 0) {
      char full_error[256];
      snprintf(full_error, sizeof(full_error), "● Generation failed: %s",
               ai_error);
      show_error_message(full_error);
    } else {
      show_error_message("● Failed to generate response");
    }
  } else {
    pthread_mutex_lock(&app.streaming.mutex);
    app.streaming.stream_id = stream_id;
    pthread_mutex_unlock(&app.streaming.mutex);
  }
}

static void add_message(message_type_t type, const char *content) {
  message_t *msg = malloc(sizeof(message_t));
  if (!msg) return;

  msg->type = type;
  msg->content = strdup(content);
  msg->tool_name = NULL;
  msg->timestamp = time(NULL);
  msg->is_streaming = false;
  msg->lines = NULL;
  msg->line_count = 0;
  msg->needs_rerender = true;
  msg->next = NULL;
  msg->tool_executions = NULL;

  if (!app.messages) {
    app.messages = msg;
  } else {
    message_t *current = app.messages;
    while (current->next) {
      current = current->next;
    }
    current->next = msg;
  }

  app.message_count++;

  if (app.chat.auto_scroll) {
    scroll_to_bottom();
  }
}

static void show_error_message(const char *message) {
  add_message(MSG_SYSTEM, message);

  if (app.ai_context) {
    const char *ai_error = ai_get_last_error(app.ai_context);
    if (ai_error && strlen(ai_error) > 0) {
      char full_error[256];
      snprintf(full_error, sizeof(full_error), "● AI Error: %s", ai_error);
      add_message(MSG_SYSTEM, full_error);
    }
  }
}

static void show_error_with_code(ai_result_t result, const char *context_msg) {
  const char *error_desc = ai_get_error_description(result);
  char full_error[256];

  if (context_msg) {
    snprintf(full_error, sizeof(full_error), "● %s: %s", context_msg,
             error_desc);
  } else {
    snprintf(full_error, sizeof(full_error), "● Error: %s", error_desc);
  }

  add_message(MSG_SYSTEM, full_error);
}

static void update_app_stats(void) {
  if (app.ai_context) {
    ai_get_stats(app.ai_context, &app.stats);
  }
}

static char *format_time(time_t timestamp) {
  struct tm *local_time = localtime(&timestamp);
  char *result = malloc(32);
  if (!result) return strdup("??:??");

  strftime(result, 32, "%H:%M", local_time);
  return result;
}

static void free_messages(void) {
  message_t *current = app.messages;
  while (current) {
    message_t *next = current->next;

    if (current->content) free(current->content);
    if (current->tool_name) free(current->tool_name);

    free_tool_executions(current->tool_executions);

    free_message_lines(current);

    free(current);
    current = next;
  }
  app.messages = NULL;
  app.message_count = 0;
  app.chat.total_lines = 0;
  app.chat.scroll_offset = 0;
  app.chat.auto_scroll = true;
}

static void cleanup_app(void) {
  free_messages();
  cleanup_ai_session();
  free_tools_config();
  cleanup_update_queue();

  pthread_mutex_destroy(&app.streaming.mutex);

  if (app.streaming.accumulated_text) {
    free(app.streaming.accumulated_text);
  }

  if (app.app_dir) {
    free(app.app_dir);
  }

  tb_shutdown();
}

int main(void) {
  init_app();

  while (app.running) {
    update_frame_timing();

    update_animations(app.timing.frame_delta_us);

    struct tb_event ev;
    int poll_result = tb_peek_event(&ev, 1);

    if (poll_result == TB_OK) {
      if (ev.type == TB_EVENT_KEY) {
        handle_input(&ev);
      } else if (ev.type == TB_EVENT_RESIZE) {
        app.needs_resize = true;
      } else if (ev.type == TB_EVENT_MOUSE) {
        handle_input(&ev);
      }
    }

    render_frame();

    wait_for_next_frame();
  }

  cleanup_app();
  return 0;
}