#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>

// Function pointer types for the AI Bridge API
typedef int (*ai_bridge_init_func)();
typedef int (*ai_bridge_check_availability_func)();
typedef char* (*ai_bridge_get_availability_reason_func)();
typedef unsigned char (*ai_bridge_create_session_func)(
    const char* instructions,
    const char* toolsJson,
    int enableGuardrails,
    int enableHistory,
    int enableStructuredResponses,
    const char* defaultSchemaJson,
    int prewarm
);
typedef unsigned char (*ai_bridge_generate_response_stream_func)(
    unsigned char sessionId,
    const char* prompt,
    double temperature,
    int maxTokens,
    void* context,
    void (*callback)(void*, const char*, void*),
    void* userData
);
typedef int (*ai_bridge_cancel_stream_func)(unsigned char streamId);
typedef void (*ai_bridge_destroy_session_func)(unsigned char sessionId);
typedef void (*ai_bridge_free_string_func)(char* ptr);
typedef char* (*ai_bridge_get_session_history_func)(unsigned char sessionId);

// Color codes for better readability
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define GREEN   "\033[32m"
#define BLUE    "\033[34m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define RED     "\033[31m"
#define DIM     "\033[2m"

// Streaming context
typedef struct {
    char* accumulated_response;
    size_t response_size;
    size_t response_capacity;
    double start_time;
    int token_count;
    int is_complete;
    int is_error;
    pthread_mutex_t mutex;
} StreamContext;

// Get current time in milliseconds
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// Estimate token count (rough approximation: ~4 chars per token on average)
int estimate_tokens(const char* text) {
    if (!text) return 0;
    int len = strlen(text);
    // Rough estimate: 1 token ≈ 4 characters for English text
    return (len + 3) / 4;
}

// Remove leading/trailing whitespace
char* trim(char* str) {
    char* start = str;
    char* end;

    // Trim leading space
    while(*start && (*start == ' ' || *start == '\t' || *start == '\n')) {
        start++;
    }

    if(*start == 0) {
        return start;
    }

    // Trim trailing space
    end = start + strlen(start) - 1;
    while(end > start && (*end == ' ' || *end == '\t' || *end == '\n')) {
        end--;
    }

    *(end + 1) = '\0';

    return start;
}

// Streaming callback function
void stream_callback(void* context, const char* token, void* userData) {
    StreamContext* ctx = (StreamContext*)context;

    pthread_mutex_lock(&ctx->mutex);

    if (token == NULL) {
        // Stream completed
        ctx->is_complete = 1;
    } else if (strncmp(token, "Error:", 6) == 0) {
        // Error occurred
        ctx->is_error = 1;
        printf(RED "\n%s" RESET, token);
    } else {
        // New token received
        size_t token_len = strlen(token);

        // Ensure capacity
        if (ctx->response_size + token_len + 1 > ctx->response_capacity) {
            ctx->response_capacity = (ctx->response_capacity + token_len + 1) * 2;
            ctx->accumulated_response = realloc(ctx->accumulated_response, ctx->response_capacity);
        }

        // Append token
        strcpy(ctx->accumulated_response + ctx->response_size, token);
        ctx->response_size += token_len;

        // Print the token immediately for streaming effect
        printf("%s", token);
        fflush(stdout);

        // Rough token counting (each space-separated word or punctuation)
        if (strchr(token, ' ') || strchr(token, '\n') ||
            strchr(token, '.') || strchr(token, ',') ||
            strchr(token, '!') || strchr(token, '?')) {
            ctx->token_count++;
        }
    }

    pthread_mutex_unlock(&ctx->mutex);
}

int main() {
    // Load the dynamic library
    void* handle = dlopen("build/dynamic/arm64/release/libaibridge.dylib", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, RED "Failed to load library: %s\n" RESET, dlerror());
        return 1;
    }

    // Load the functions
    ai_bridge_init_func ai_bridge_init =
        (ai_bridge_init_func)dlsym(handle, "ai_bridge_init");
    ai_bridge_check_availability_func ai_bridge_check_availability =
        (ai_bridge_check_availability_func)dlsym(handle, "ai_bridge_check_availability");
    ai_bridge_get_availability_reason_func ai_bridge_get_availability_reason =
        (ai_bridge_get_availability_reason_func)dlsym(handle, "ai_bridge_get_availability_reason");
    ai_bridge_create_session_func ai_bridge_create_session =
        (ai_bridge_create_session_func)dlsym(handle, "ai_bridge_create_session");
    ai_bridge_generate_response_stream_func ai_bridge_generate_response_stream =
        (ai_bridge_generate_response_stream_func)dlsym(handle, "ai_bridge_generate_response_stream");
    ai_bridge_cancel_stream_func ai_bridge_cancel_stream =
        (ai_bridge_cancel_stream_func)dlsym(handle, "ai_bridge_cancel_stream");
    ai_bridge_destroy_session_func ai_bridge_destroy_session =
        (ai_bridge_destroy_session_func)dlsym(handle, "ai_bridge_destroy_session");
    ai_bridge_free_string_func ai_bridge_free_string =
        (ai_bridge_free_string_func)dlsym(handle, "ai_bridge_free_string");
    ai_bridge_get_session_history_func ai_bridge_get_session_history =
        (ai_bridge_get_session_history_func)dlsym(handle, "ai_bridge_get_session_history");

    if (!ai_bridge_init || !ai_bridge_check_availability ||
        !ai_bridge_create_session || !ai_bridge_generate_response_stream ||
        !ai_bridge_destroy_session || !ai_bridge_free_string) {
        fprintf(stderr, RED "Failed to load functions: %s\n" RESET, dlerror());
        dlclose(handle);
        return 1;
    }

    // Initialize the AI Bridge
    printf(CYAN "🚀 Initializing AI Bridge...\n" RESET);
    if (!ai_bridge_init()) {
        fprintf(stderr, RED "Failed to initialize AI Bridge\n" RESET);
        dlclose(handle);
        return 1;
    }

    // Check availability
    printf(CYAN "🔍 Checking Apple Intelligence availability...\n" RESET);
    int availability = ai_bridge_check_availability();
    if (availability != 1) {  // 1 = available
        char* reason = ai_bridge_get_availability_reason();
        if (reason) {
            fprintf(stderr, RED "❌ Apple Intelligence not available: %s\n" RESET, reason);
            ai_bridge_free_string(reason);
        } else {
            fprintf(stderr, RED "❌ Apple Intelligence not available (status: %d)\n" RESET, availability);
        }
        dlclose(handle);
        return 1;
    }
    printf(GREEN "✅ Apple Intelligence is available!\n" RESET);

    // Create a session
    printf(CYAN "📝 Creating AI session...\n" RESET);
    unsigned char sessionId = ai_bridge_create_session(
        "You are a helpful assistant that provides thoughtful and concise answers.",  // instructions
        NULL,   // no tools
        1,      // enable guardrails
        1,      // enable history
        0,      // no structured responses
        NULL,   // no default schema
        1       // prewarm
    );

    if (sessionId == 0) {
        fprintf(stderr, RED "Failed to create session\n" RESET);
        dlclose(handle);
        return 1;
    }
    printf(GREEN "✅ Session created with ID: %d\n" RESET, sessionId);

    // Print instructions
    printf("\n" BOLD "💬 Interactive Streaming Chat with Apple Intelligence\n" RESET);
    printf("═══════════════════════════════════════════════════\n");
    printf("Type your message and press Enter to send.\n");
    printf("Responses will stream in real-time as they're generated.\n");
    printf("\nCommands:\n");
    printf("  " YELLOW "/quit" RESET " or " YELLOW "/exit" RESET " - Exit the chat\n");
    printf("  " YELLOW "/history" RESET " - Show conversation history\n");
    printf("  " YELLOW "/clear" RESET " - Clear conversation history\n");
    printf("  " YELLOW "/temp <value>" RESET " - Set temperature (0.0-1.0)\n");
    printf("  " YELLOW "/tokens <value>" RESET " - Set max tokens\n");
    printf("  " YELLOW "/cancel" RESET " - Cancel current stream (if running)\n");
    printf("═══════════════════════════════════════════════════\n");

    char input[4096];
    double temperature = 0.7;
    int maxTokens = 1000;
    int totalPromptTokens = 0;
    int totalResponseTokens = 0;
    double totalTime = 0;
    int numResponses = 0;
    unsigned char currentStreamId = 0;

    while (1) {
        // Get user input
        printf(BLUE "\nYou> " RESET);
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Trim the input
        char* trimmed = trim(input);

        // Check for empty input
        if (strlen(trimmed) == 0) {
            continue;
        }

        // Check for commands
        if (strcmp(trimmed, "/quit") == 0 || strcmp(trimmed, "/exit") == 0) {
            printf(YELLOW "👋 Goodbye!\n" RESET);
            break;
        }

        if (strcmp(trimmed, "/cancel") == 0) {
            if (currentStreamId > 0 && ai_bridge_cancel_stream) {
                if (ai_bridge_cancel_stream(currentStreamId)) {
                    printf(YELLOW "⏹️  Stream cancelled.\n" RESET);
                } else {
                    printf(YELLOW "No active stream to cancel.\n" RESET);
                }
            } else {
                printf(YELLOW "No active stream to cancel.\n" RESET);
            }
            continue;
        }

        if (strcmp(trimmed, "/history") == 0) {
            if (ai_bridge_get_session_history) {
                char* history = ai_bridge_get_session_history(sessionId);
                if (history) {
                    printf(CYAN "📜 Conversation History:\n%s\n" RESET, history);
                    ai_bridge_free_string(history);
                } else {
                    printf(YELLOW "No history available.\n" RESET);
                }
            } else {
                printf(YELLOW "History function not available.\n" RESET);
            }
            continue;
        }

        if (strcmp(trimmed, "/clear") == 0) {
            // Recreate session to clear history
            ai_bridge_destroy_session(sessionId);
            sessionId = ai_bridge_create_session(
                "You are a helpful assistant that provides thoughtful and concise answers.",
                NULL, 1, 1, 0, NULL, 1
            );
            printf(GREEN "✨ History cleared, new session started.\n" RESET);
            totalPromptTokens = 0;
            totalResponseTokens = 0;
            totalTime = 0;
            numResponses = 0;
            continue;
        }

        if (strncmp(trimmed, "/temp ", 6) == 0) {
            double newTemp = atof(trimmed + 6);
            if (newTemp >= 0.0 && newTemp <= 1.0) {
                temperature = newTemp;
                printf(GREEN "🌡️  Temperature set to %.2f\n" RESET, temperature);
            } else {
                printf(RED "Invalid temperature. Must be between 0.0 and 1.0\n" RESET);
            }
            continue;
        }

        if (strncmp(trimmed, "/tokens ", 8) == 0) {
            int newTokens = atoi(trimmed + 8);
            if (newTokens > 0 && newTokens <= 10000) {
                maxTokens = newTokens;
                printf(GREEN "🎯 Max tokens set to %d\n" RESET, maxTokens);
            } else {
                printf(RED "Invalid token count. Must be between 1 and 10000\n" RESET);
            }
            continue;
        }

        // Estimate prompt tokens
        int promptTokens = estimate_tokens(trimmed);
        totalPromptTokens += promptTokens;

        // Initialize streaming context
        StreamContext ctx = {
            .accumulated_response = malloc(1024),
            .response_size = 0,
            .response_capacity = 1024,
            .start_time = get_time_ms(),
            .token_count = 0,
            .is_complete = 0,
            .is_error = 0,
            .mutex = PTHREAD_MUTEX_INITIALIZER
        };
        ctx.accumulated_response[0] = '\0';

        // Start streaming response
        printf(GREEN "\nAI> " RESET);
        fflush(stdout);

        currentStreamId = ai_bridge_generate_response_stream(
            sessionId,
            trimmed,
            temperature,
            maxTokens,
            &ctx,
            stream_callback,
            NULL
        );

        if (currentStreamId == 0) {
            fprintf(stderr, RED "\n❌ Failed to start stream\n" RESET);
            free(ctx.accumulated_response);
            continue;
        }

        // Wait for stream to complete
        while (!ctx.is_complete && !ctx.is_error) {
            usleep(10000); // Sleep for 10ms
        }

        printf("\n\n");  // Add extra newline for spacing

        // Calculate and display statistics
        double endTime = get_time_ms();
        double elapsedTime = (endTime - ctx.start_time) / 1000.0; // Convert to seconds

        if (!ctx.is_error && ctx.response_size > 0) {
            // Calculate statistics
            int responseTokens = estimate_tokens(ctx.accumulated_response);
            totalResponseTokens += responseTokens;
            totalTime += elapsedTime;
            numResponses++;

            double tokensPerSecond = responseTokens / elapsedTime;

            // Print statistics
            printf(DIM "📊 Stats: ");
            printf("%.2fs | ~%d tokens | %.1f tok/s",
                   elapsedTime, responseTokens, tokensPerSecond);

            if (numResponses > 1) {
                double avgTokensPerSec = totalResponseTokens / totalTime;
                printf(" | Session avg: %.1f tok/s", avgTokensPerSec);
            }

            // Show first token time if we tracked it
            if (ctx.token_count > 0) {
                printf(" | Streaming: ✓");
            }

            printf(RESET "\n");
        }

        // Clean up context
        free(ctx.accumulated_response);
        pthread_mutex_destroy(&ctx.mutex);
        currentStreamId = 0;
    }

    // Print session statistics
    if (numResponses > 0) {
        printf("\n" BOLD "📈 Session Statistics\n" RESET);
        printf("═══════════════════════════════════════════════════\n");
        printf("Total responses: %d\n", numResponses);
        printf("Total prompt tokens: ~%d\n", totalPromptTokens);
        printf("Total response tokens: ~%d\n", totalResponseTokens);
        printf("Total generation time: %.2fs\n", totalTime);
        printf("Average tokens/sec: %.1f\n", totalResponseTokens / totalTime);
        printf("═══════════════════════════════════════════════════\n");
    }

    // Clean up
    printf(CYAN "\n🧹 Cleaning up...\n" RESET);
    ai_bridge_destroy_session(sessionId);
    dlclose(handle);

    return 0;
}