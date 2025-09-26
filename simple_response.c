#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

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
typedef char* (*ai_bridge_generate_response_func)(
    unsigned char sessionId,
    const char* prompt,
    double temperature,
    int maxTokens
);
typedef void (*ai_bridge_destroy_session_func)(unsigned char sessionId);
typedef void (*ai_bridge_free_string_func)(char* ptr);

int main() {
    // Load the dynamic library
    void* handle = dlopen("build/dynamic/arm64/release/libaibridge.dylib", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Failed to load library: %s\n", dlerror());
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
    ai_bridge_generate_response_func ai_bridge_generate_response =
        (ai_bridge_generate_response_func)dlsym(handle, "ai_bridge_generate_response");
    ai_bridge_destroy_session_func ai_bridge_destroy_session =
        (ai_bridge_destroy_session_func)dlsym(handle, "ai_bridge_destroy_session");
    ai_bridge_free_string_func ai_bridge_free_string =
        (ai_bridge_free_string_func)dlsym(handle, "ai_bridge_free_string");

    if (!ai_bridge_init || !ai_bridge_check_availability ||
        !ai_bridge_create_session || !ai_bridge_generate_response ||
        !ai_bridge_destroy_session || !ai_bridge_free_string) {
        fprintf(stderr, "Failed to load functions: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }

    // Initialize the AI Bridge
    printf("Initializing AI Bridge...\n");
    if (!ai_bridge_init()) {
        fprintf(stderr, "Failed to initialize AI Bridge\n");
        dlclose(handle);
        return 1;
    }

    // Check availability
    printf("Checking Apple Intelligence availability...\n");
    int availability = ai_bridge_check_availability();
    if (availability != 1) {  // 1 = available
        char* reason = ai_bridge_get_availability_reason();
        if (reason) {
            fprintf(stderr, "Apple Intelligence not available: %s\n", reason);
            ai_bridge_free_string(reason);
        } else {
            fprintf(stderr, "Apple Intelligence not available (status: %d)\n", availability);
        }
        dlclose(handle);
        return 1;
    }
    printf("Apple Intelligence is available!\n");

    // Create a session
    printf("Creating AI session...\n");
    unsigned char sessionId = ai_bridge_create_session(
        "You are a helpful assistant that provides thoughtful and concise answers.",  // instructions
        NULL,   // no tools
        1,      // enable guardrails (though they may not be used in new API)
        1,      // enable history
        0,      // no structured responses
        NULL,   // no default schema
        1       // prewarm
    );

    if (sessionId == 0) {
        fprintf(stderr, "Failed to create session\n");
        dlclose(handle);
        return 1;
    }
    printf("Session created with ID: %d\n", sessionId);

    // Generate response
    const char* prompt = "What is the meaning of life?";
    printf("\nPrompt: %s\n", prompt);
    printf("Generating response...\n\n");

    char* response = ai_bridge_generate_response(
        sessionId,
        prompt,
        0.7,    // temperature
        500     // max tokens
    );

    if (response) {
        printf("Response: %s\n", response);
        ai_bridge_free_string(response);
    } else {
        fprintf(stderr, "Failed to generate response\n");
    }

    // Clean up
    printf("\nCleaning up...\n");
    ai_bridge_destroy_session(sessionId);
    dlclose(handle);

    return 0;
}