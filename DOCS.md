# AI Bridge Python Wrapper Documentation

A complete Python interface to Apple Intelligence on macOS 26+ through the FoundationModels framework.

## Quick Start

```python
from ai_bridge import AIBridge, AIAvailabilityStatus

# Initialize the bridge
bridge = AIBridge()

# Check availability
status, reason = bridge.check_availability()
if status != AIAvailabilityStatus.AVAILABLE:
    print(f"Apple Intelligence not available: {reason}")
    exit(1)

# Get supported languages
languages = bridge.get_supported_languages()
print(f"Supported languages: {', '.join(languages[:3])}...")

# Create a session with full configuration
session = bridge.create_session(
    instructions="You are a helpful assistant",
    enable_structured_responses=True,
    prewarm=True
)

# Generate a response
response = session.generate_response("Tell me a joke")
print(response)

# Generate structured JSON response
schema = {
    "type": "object",
    "properties": {
        "setup": {"type": "string"},
        "punchline": {"type": "string"}
    }
}
structured = session.generate_structured_response(
    "Tell me a joke in JSON format",
    schema=schema
)
print(structured["object"])

# Clean up
session.destroy()
```

### Minimal Interactive Chat

```python
from ai_bridge import AIBridge

# Initialize and create session
bridge = AIBridge()
session = bridge.create_session(
    instructions=(
        "You are a helpful, friendly, and knowledgeable assistant. "
        "Provide clear, concise, and accurate responses. "
        "Be conversational while remaining professional."
    ),
    enable_history=True
)

# Chat loop
print("Chat with AI (type 'quit' to exit)")
while True:
    prompt = input("\nYou: ").strip()
    if prompt.lower() == 'quit':
        break

    # Stream the response
    print("AI: ", end='', flush=True)
    stream_id = session.stream_response(
        prompt,
        lambda token: print(token or '', end='', flush=True)
    )
    bridge.wait_for_stream(stream_id)
    print()  # New line after response

# Clean up
session.destroy()
```

## Installation

### Requirements

- **macOS 26.0** (Sequoia) or later
- **Apple Intelligence** enabled in System Settings
- **Python 3.7+**
- **Xcode 16.4+** with Swift 6.2 or later
- **Apple Silicon Mac** (M1/M2/M3/M4) or Intel Mac with Apple Intelligence support

### Building the Library

The project uses a Makefile to build the native Swift/C bridge library.

#### Quick Build
```bash
# Clean and build everything
make clean && make

# Or just build
make

# Building the C files
clang -o chat chat.c -L. -Wl,-rpath,build/dynamic/arm64/release -lpthread
```

#### Build Output
The build process creates:
- `libaibridge.dylib` - Swift bridge to FoundationModels framework
- `libai.dylib` - C wrapper library (optional)
- `momo` - Command-line interface (optional)

All libraries are built to: `build/dynamic/arm64/release/`

#### Build Options
```bash
# Debug build with symbols
make dynamic-dbg

# Release build (default)
make dynamic-rel

# Clean build artifacts
make clean

# Build only the Swift bridge
swiftc -emit-library -o libaibridge.dylib bridge.swift

# Verify the build
ls -la build/dynamic/arm64/release/*.dylib
```

#### Troubleshooting Build Issues

**"SDK not found" error:**
```bash
# Verify Xcode is installed and selected
xcode-select --print-path
# If needed, set the correct Xcode
sudo xcode-select -s /Applications/Xcode.app
```

**"Swift version mismatch" error:**
```bash
# Check your Swift version
swift --version
# Should be 6.2 or later for macOS 26
```

**"FoundationModels not found" error:**
- Ensure you're on macOS 26 or later
- Verify Apple Intelligence is enabled
- Check SDK path: `xcrun --show-sdk-path`

### Python Setup

Once the library is built:

```python
# Import the wrapper
import ai_bridge

# Or with custom library path
from pathlib import Path
from ai_bridge import AIBridge

# Set via environment variable
import os
os.environ['AI_BRIDGE_LIBRARY'] = '/custom/path/libaibridge.dylib'
bridge = AIBridge()

# Or pass directly
bridge = AIBridge(library_path=Path("/custom/path/libaibridge.dylib"))
```

## Core Components

### AIBridge

The main interface to the native library with complete API coverage.

```python
bridge = AIBridge(library_path=None)
```

**Parameters:**
- `library_path` (Optional[Path]): Path to libaibridge.dylib. Auto-detects from:
  - Environment variable `AI_BRIDGE_LIBRARY`
  - Build directories (arm64/x86_64)
  - System paths (/usr/local/lib, ~/.local/lib)

**Key Improvements:**
- ✅ Thread-safe with instance-level context management (no global state)
- ✅ Automatic library discovery
- ✅ Complete API coverage
- ✅ Better error handling with specific exceptions

### Methods

#### `check_availability() -> Tuple[AIAvailabilityStatus, Optional[str]]`
Check if Apple Intelligence is available.

```python
status, reason = bridge.check_availability()
if status == AIAvailabilityStatus.AVAILABLE:
    print("Ready to use!")
elif status == AIAvailabilityStatus.MODEL_NOT_READY:
    print(f"Model downloading: {reason}")
```

#### `get_supported_languages() -> List[str]`
**NEW:** Get list of supported languages.

```python
languages = bridge.get_supported_languages()
print(f"Supports {len(languages)} languages:")
for lang in languages:
    print(f"  - {lang}")
```

#### `create_session(**kwargs) -> AISession`
Create a new AI session with full configuration options.

```python
session = bridge.create_session(
    instructions="You are a helpful assistant",
    enable_history=True,
    enable_structured_responses=True,
    default_schema={"type": "object"},
    prewarm=True
)
```

**Enhanced Parameters:**
- `instructions` (Optional[str]): System instructions for the AI
- `enable_history` (bool): Whether to maintain conversation history (default: True)
- `enable_structured_responses` (bool): **NEW** - Enable JSON schema validation (default: False)
- `default_schema` (Optional[Dict]): **NEW** - Default schema for structured responses
- `prewarm` (bool): Preload resources for faster first response (default: True)

#### `wait_for_stream(stream_id, timeout=60.0) -> bool`
Wait for a stream to complete.

```python
if bridge.wait_for_stream(stream_id, timeout=30.0):
    print("Stream completed successfully")
```

**Note:** Auto-cleanup parameter removed for better control

#### `is_stream_error(stream_id) -> bool`
Check if a stream encountered an error.

```python
if bridge.is_stream_error(stream_id):
    print("Stream had an error")
```

#### `cleanup()`
Clean up all resources. Called automatically on exit.

### AISession

Enhanced session class with complete feature support.

#### `generate_response(prompt, temperature=1.0, max_tokens=1000) -> str`
Generate a response synchronously.

```python
response = session.generate_response(
    "What is the capital of France?",
    temperature=0.3,  # Low temperature for factual response
    max_tokens=50
)
```

**Enhanced Error Handling:**
- `SessionDestroyedError`: Session was destroyed
- `PromptTooLongError`: Prompt exceeds 100k character limit
- `ValueError`: Invalid parameters

#### `generate_structured_response(prompt, schema=None, temperature=1.0, max_tokens=1000) -> Dict`
**NEW:** Generate a structured JSON response.

```python
schema = {
    "type": "object",
    "properties": {
        "name": {"type": "string"},
        "age": {"type": "integer"},
        "skills": {
            "type": "array",
            "items": {"type": "string"}
        }
    },
    "required": ["name", "age"]
}

result = session.generate_structured_response(
    "Generate a profile for a software developer",
    schema=schema,
    temperature=1.0
)

print(result["text"])    # Human-readable version
print(result["object"])  # Structured JSON object
```

#### `stream_response(prompt, callback, temperature=1.0, max_tokens=1000) -> Optional[int]`
Stream a response with real-time tokens.

```python
def on_token(token):
    if token is None:
        print("\n[Complete]")
    else:
        print(token, end='', flush=True)

stream_id = session.stream_response(
    "Write a story",
    callback=on_token,
    temperature=1.5  # Higher temperature for creativity
)
```

#### `stream_structured_response(prompt, callback, schema=None, temperature=1.0, max_tokens=1000) -> Optional[int]`
**NEW:** Stream a structured response (delivers complete JSON when ready).

```python
def on_complete(json_str):
    if json_str and not json_str.startswith("Error:"):
        data = json.loads(json_str)
        print(f"Received structured data: {data}")

stream_id = session.stream_structured_response(
    "Generate user data",
    callback=on_complete,
    schema=user_schema
)
```


#### `cancel_stream(stream_id) -> bool`
Cancel an active stream.

```python
if session.cancel_stream(stream_id):
    print("Stream cancelled")
```

#### `get_history() -> Optional[str]`
Get conversation history as JSON.

```python
history = session.get_history()
if history:
    messages = json.loads(history)
    print(f"Conversation has {len(messages)} messages")
```

#### `clear_history() -> bool`
Clear the conversation history.

```python
if session.clear_history():
    print("History cleared")
```

#### `add_message_to_history(role, content) -> bool`
**NEW:** Manually add a message to history.

```python
# Add context to the conversation
session.add_message_to_history("system", "User prefers technical answers")
session.add_message_to_history("user", "Previous question about Python")
session.add_message_to_history("assistant", "I explained Python's GIL")
```

**Roles:**
- `"user"` - User messages
- `"assistant"` - AI responses
- `"system"` - System context
- `"tool"` - Tool execution results

#### `destroy()`
Destroy the session and free resources. Safe to call multiple times.

### Context Manager Support

Sessions can be used as context managers:

```python
with bridge.create_session() as session:
    response = session.generate_response("Hello!")
    print(response)
# Session automatically destroyed
```

## Exception Hierarchy

Enhanced error handling with specific exception types:

```python
from ai_bridge import (
    AIBridgeError,           # Base exception
    SessionDestroyedError,   # Session was destroyed
    ModelUnavailableError,   # AI not available
    PromptTooLongError,      # Prompt exceeds limit
    StreamError             # Streaming operation failed
)

try:
    response = session.generate_response(prompt)
except SessionDestroyedError:
    print("Session was already destroyed")
except PromptTooLongError as e:
    print(f"Prompt too long: {e}")
except ModelUnavailableError as e:
    print(f"Model not available: {e}")
except AIBridgeError as e:
    print(f"Other error: {e}")
```

## Limits and Validation

The wrapper includes validation constants:

```python
from ai_bridge import Limits

print(f"Temperature range: {Limits.MIN_TEMPERATURE}-{Limits.MAX_TEMPERATURE}")
print(f"Token range: {Limits.MIN_TOKENS}-{Limits.MAX_TOKENS}")
print(f"Max prompt length: {Limits.MAX_PROMPT_LENGTH} characters")
print(f"Max sessions: {Limits.MAX_SESSIONS_PER_BRIDGE}")
```


## Structured Response Generation

Generate validated JSON responses:

```python
# Define a complex schema
person_schema = {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "type": "object",
    "properties": {
        "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 100
        },
        "age": {
            "type": "integer",
            "minimum": 0,
            "maximum": 150
        },
        "email": {
            "type": "string",
            "format": "email"
        },
        "interests": {
            "type": "array",
            "items": {"type": "string"},
            "minItems": 1,
            "maxItems": 5
        }
    },
    "required": ["name", "age", "interests"]
}

# Generate structured data
result = session.generate_structured_response(
    prompt="Create a profile for a 28-year-old software engineer named Alice",
    schema=person_schema,
    temperature=0.3  # Low temperature for consistent structure
)

# Access the structured data
person = result["object"]
print(f"Name: {person['name']}")
print(f"Age: {person['age']}")
print(f"Interests: {', '.join(person['interests'])}")
```

## Thread Safety

The wrapper is fully thread-safe with no global state:

```python
import threading
import concurrent.futures

# Each bridge instance is independent
def worker(worker_id):
    # Create separate bridge per thread (recommended)
    bridge = AIBridge()
    session = bridge.create_session(
        instructions=f"You are assistant #{worker_id}"
    )

    response = session.generate_response(
        f"Say hello from worker {worker_id}"
    )

    session.destroy()
    bridge.cleanup()
    return response

# Run concurrent workers
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
    futures = [executor.submit(worker, i) for i in range(5)]
    results = [f.result() for f in futures]

for i, result in enumerate(results):
    print(f"Worker {i}: {result}")
```

### Thread Safety Guarantees

1. **No global state**: Each AIBridge instance manages its own contexts
2. **Thread-safe operations**: All methods use proper locking (RLock)
3. **Safe callbacks**: Streaming callbacks can be called from any thread
4. **Weak references**: Prevents circular references and memory leaks
5. **Session isolation**: Sessions can be used independently from different threads

## Performance Tips

1. **Use prewarm**: Create sessions with `prewarm=True` for faster first response
2. **Reuse sessions**: Don't create new sessions for each request
3. **Stream for long responses**: Use streaming for better perceived performance
4. **Batch operations**: Create multiple sessions once, reuse them
5. **Appropriate temperatures**:
   - 0.0-0.3: Deterministic, factual tasks
   - 0.3-0.7: Balanced responses
   - 0.7-1.2: Creative tasks
   - 1.2-2.0: Highly creative/experimental
6. **Manage token limits**: Set reasonable `max_tokens` to control response length
7. **Use structured responses**: For data extraction, use schemas for consistency

## Complete Example

```python
#!/usr/bin/env python3
"""Advanced chat with Apple Intelligence using all features"""

import json
from ai_bridge import (
    AIBridge, AIAvailabilityStatus,
    AIBridgeError, SessionDestroyedError
)


def main():
    # Initialize with auto-discovery
    try:
        bridge = AIBridge()
    except AIBridgeError as e:
        print(f"Failed to initialize: {e}")
        return

    # Check availability
    status, reason = bridge.check_availability()
    if status != AIAvailabilityStatus.AVAILABLE:
        print(f"Not available: {reason}")
        return

    # Show supported languages
    languages = bridge.get_supported_languages()
    print(f"Supports {len(languages)} languages\n")


    # Create enhanced session
    with bridge.create_session(
        instructions="You are a helpful assistant.",
        enable_structured_responses=True,
        prewarm=True
    ) as session:

        print("Enhanced Chat (type 'quit' to exit)\n")
        print("Commands: /structured for JSON response, 'quit' to exit\n")

        while True:
            prompt = input("You: ").strip()
            if prompt.lower() in ['quit', 'exit']:
                break

            if prompt.startswith('/structured'):
                # Demonstrate structured response
                schema = {
                    "type": "object",
                    "properties": {
                        "summary": {"type": "string"},
                        "confidence": {"type": "number", "minimum": 0, "maximum": 1}
                    }
                }

                result = session.generate_structured_response(
                    "Summarize the previous conversation",
                    schema=schema
                )
                print(f"Structured: {json.dumps(result['object'], indent=2)}")
                continue

            # Stream response
            try:
                print("AI: ", end='', flush=True)

                def callback(token):
                    if token:
                        print(token, end='', flush=True)
                    else:
                        print()

                stream_id = session.stream_response(prompt, callback)
                if stream_id:
                    bridge.wait_for_stream(stream_id)
                print()

            except (ValueError, AIBridgeError) as e:
                print(f"\nError: {e}")

    print("\nGoodbye!")

if __name__ == "__main__":
    main()
```

## Migration from Original Wrapper

To migrate from `ai_bridge.py` to `ai_bridge.py`:

```python
# Old import
from ai_bridge import AIBridge, AISession

# New import
from ai_bridge import AIBridge, AISession

# New features available:
languages = bridge.get_supported_languages()
structured = session.generate_structured_response(prompt, schema)
session.add_message_to_history(role, content)

# Enhanced session creation:
session = bridge.create_session(
    tools=[...],                      # NEW
    enable_structured_responses=True,  # NEW
    default_schema={...}              # NEW
)
```

## Troubleshooting

### "Library not found"
- Run `make` to build the library
- Set `AI_BRIDGE_LIBRARY` environment variable
- Check standard paths: `~/.local/lib`, `/usr/local/lib`
- Pass explicit path: `AIBridge(library_path="/path/to/libaibridge.dylib")`

### "Apple Intelligence not available"
- Ensure macOS 26.0 or later
- Enable Apple Intelligence in System Settings
- Wait for models to download completely
- Check specific reason: `bridge.check_availability()`

### "Guardrail violation: Content blocked by safety filters"
- **This is Apple's mandatory content safety system**
- Guardrails cannot be disabled programmatically
- Common triggers: violence, sexual content, self-harm, illegal activities
- Try rephrasing or splitting content

### "Session has been destroyed"
- Don't reuse sessions after calling `destroy()`
- Use context managers for automatic cleanup
- Catch `SessionDestroyedError` specifically

### "Prompt too long"
- Maximum prompt length is 100,000 characters (~25k tokens)
- Catch `PromptTooLongError` specifically
- Split long prompts into chunks

### Thread safety issues
- Each thread should use its own AIBridge instance (recommended)
- Or share one AIBridge but create separate sessions per thread
- Callbacks are thread-safe but run on background threads


## API Reference Summary

### Classes
- `AIBridge` - Main library interface (thread-safe, no global state)
- `AISession` - Conversation session with full feature support
- `AIAvailabilityStatus` - Availability status enum
- `AIBridgeError` - Base exception
- `SessionDestroyedError` - Session destroyed exception
- `ModelUnavailableError` - Model not available exception
- `PromptTooLongError` - Prompt length exception
- `StreamError` - Streaming error exception
- `Limits` - Validation constants dataclass

### Key Methods
**AIBridge:**
- `check_availability()` - Check if available
- `get_supported_languages()` - **NEW** - Get language list
- `create_session()` - Create session with full configuration
- `wait_for_stream()` - Wait for stream completion
- `is_stream_error()` - Check if stream errored
- `cleanup()` - Clean up all resources

**AISession:**
- `generate_response()` - Generate text synchronously
- `generate_structured_response()` - **NEW** - Generate JSON with schema
- `stream_response()` - Stream text with callbacks
- `stream_structured_response()` - **NEW** - Stream structured JSON
- `cancel_stream()` - Cancel streaming
- `get_history()` - Get conversation history
- `clear_history()` - Clear conversation history
- `add_message_to_history()` - **NEW** - Add message manually
- `destroy()` - Clean up session

### Parameters
- `prompt` - Input text (max 100k characters)
- `temperature` - Randomness (0.0-2.0)
- `max_tokens` - Response limit (1-100,000)
- `instructions` - System prompt for behavior
- `schema` - JSON schema for structured responses
- `enable_history` - Track conversation
- `enable_structured_responses` - Enable JSON schemas
- `default_schema` - Default response schema
- `prewarm` - Preload for performance

## Version Information

- **Wrapper Version**: 1.0.0
- **API Coverage**: 100% (all C library functions)
- **Thread Safety**: Full (no global state)
- **Python Support**: 3.7+
- **macOS Support**: 26.0+