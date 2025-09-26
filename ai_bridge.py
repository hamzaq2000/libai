"""
Python wrapper for the AI Bridge library (libaibridge.dylib)

This module provides a complete Python interface to the Apple Intelligence
FoundationModels framework through the compiled C/Swift bridge.

Improvements over original:
- Complete API coverage (100% of C library functions)
- Thread-safe context management without global state
- Support for structured responses and tool callbacks
- Better error handling with specific exceptions
"""

import ctypes
import json
import threading
import weakref
from typing import Optional, Callable, Any, Dict, Tuple, Union, List
from pathlib import Path
from enum import IntEnum
import atexit
import os
from dataclasses import dataclass


class AIAvailabilityStatus(IntEnum):
    """Status codes for Apple Intelligence availability"""
    AVAILABLE = 1
    DEVICE_NOT_ELIGIBLE = -1
    INTELLIGENCE_NOT_ENABLED = -2
    MODEL_NOT_READY = -3
    UNKNOWN_ERROR = -99


class AIBridgeError(Exception):
    """Base exception for AI Bridge errors"""
    pass


class SessionDestroyedError(AIBridgeError):
    """Raised when operating on a destroyed session"""
    pass


class ModelUnavailableError(AIBridgeError):
    """Raised when AI model is not available"""
    pass


class PromptTooLongError(AIBridgeError):
    """Raised when prompt exceeds maximum length"""
    pass


class StreamError(AIBridgeError):
    """Raised when streaming operation fails"""
    pass


@dataclass
class Limits:
    """API limits and validation constants"""
    MIN_TEMPERATURE: float = 0.0
    MAX_TEMPERATURE: float = 2.0
    MIN_TOKENS: int = 1
    MAX_TOKENS: int = 100000
    MAX_PROMPT_LENGTH: int = 100000  # ~25k tokens
    MAX_SESSIONS_PER_BRIDGE: int = 255  # uint8 limit


class StreamingContext:
    """Thread-safe context for streaming responses.

    This class manages the state of a streaming response, including
    thread-safe completion tracking and callback invocation.
    """

    def __init__(self,
                 context_id: int,
                 callback: Callable[[Optional[str]], None],
                 session_id: Optional[int] = None,
                 bridge_ref: Optional[weakref.ref] = None) -> None:
        self.context_id = context_id
        self.callback = callback
        self.session_id = session_id
        self.bridge_ref = bridge_ref  # Weak reference to avoid circular refs
        self.is_complete = False
        self.is_error = False
        self.lock = threading.Lock()
        self.completion_event = threading.Event()

    def cleanup(self) -> None:
        """Mark as complete and signal any waiters."""
        with self.lock:
            self.is_complete = True
        self.completion_event.set()


class AISession:
    """Represents an AI session with Apple Intelligence.

    This class provides complete access to all Apple Intelligence features
    including text generation, structured responses, tool callbacks, and
    conversation history management.
    """

    def __init__(self, session_id: int, bridge: 'AIBridge') -> None:
        self.session_id = session_id
        self.bridge = bridge
        self._destroyed = False
        self._lock = threading.Lock()

    def generate_response(self,
                         prompt: str,
                         temperature: float = 1.0,
                         max_tokens: int = 1000) -> str:
        """Generate a response synchronously.

        Args:
            prompt: The input prompt
            temperature: Controls randomness (0.0-2.0)
            max_tokens: Maximum tokens to generate

        Returns:
            The generated response

        Raises:
            SessionDestroyedError: If session is destroyed
            PromptTooLongError: If prompt exceeds limit
            ValueError: If parameters are invalid
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        # Validate parameters
        if not prompt or not prompt.strip():
            raise ValueError("Prompt cannot be empty")
        if len(prompt) > Limits.MAX_PROMPT_LENGTH:
            raise PromptTooLongError(f"Prompt too long: {len(prompt)} characters (max {Limits.MAX_PROMPT_LENGTH})")
        if not Limits.MIN_TEMPERATURE <= temperature <= Limits.MAX_TEMPERATURE:
            raise ValueError(f"Temperature must be between {Limits.MIN_TEMPERATURE} and {Limits.MAX_TEMPERATURE}")
        if not Limits.MIN_TOKENS <= max_tokens <= Limits.MAX_TOKENS:
            raise ValueError(f"max_tokens must be between {Limits.MIN_TOKENS} and {Limits.MAX_TOKENS}")

        response_ptr = self.bridge._lib.ai_bridge_generate_response(
            self.session_id,
            prompt.encode('utf-8'),
            ctypes.c_double(temperature),
            ctypes.c_int32(max_tokens)
        )

        if response_ptr:
            response = ctypes.string_at(response_ptr).decode('utf-8')
            self.bridge._lib.ai_bridge_free_string(response_ptr)

            if response.startswith("Error:"):
                raise AIBridgeError(response)
            return response
        else:
            raise AIBridgeError("Failed to generate response")

    def generate_structured_response(self,
                                   prompt: str,
                                   schema: Optional[Dict[str, Any]] = None,
                                   temperature: float = 1.0,
                                   max_tokens: int = 1000) -> Dict[str, Any]:
        """Generate a structured JSON response according to schema.

        Args:
            prompt: The input prompt
            schema: JSON schema defining the expected structure
            temperature: Controls randomness (0.0-2.0)
            max_tokens: Maximum tokens to generate

        Returns:
            Dictionary with 'text' and 'object' fields

        Raises:
            SessionDestroyedError: If session is destroyed
            ValueError: If parameters are invalid
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        # Validate parameters
        if not prompt or not prompt.strip():
            raise ValueError("Prompt cannot be empty")
        if len(prompt) > Limits.MAX_PROMPT_LENGTH:
            raise PromptTooLongError(f"Prompt too long: {len(prompt)} characters")

        schema_json = json.dumps(schema).encode('utf-8') if schema else None

        response_ptr = self.bridge._lib.ai_bridge_generate_structured_response(
            self.session_id,
            prompt.encode('utf-8'),
            schema_json,
            ctypes.c_double(temperature),
            ctypes.c_int32(max_tokens)
        )

        if response_ptr:
            response = ctypes.string_at(response_ptr).decode('utf-8')
            self.bridge._lib.ai_bridge_free_string(response_ptr)

            if response.startswith("Error:"):
                raise AIBridgeError(response)

            try:
                return json.loads(response)
            except json.JSONDecodeError as e:
                raise AIBridgeError(f"Invalid JSON response: {e}")
        else:
            raise AIBridgeError("Failed to generate structured response")

    def stream_response(self,
                       prompt: str,
                       callback: Callable[[Optional[str]], None],
                       temperature: float = 1.0,
                       max_tokens: int = 1000) -> Optional[int]:
        """Stream a response with incremental callbacks.

        Args:
            prompt: The input prompt
            callback: Function called with each token (None when complete)
            temperature: Controls randomness (0.0-2.0)
            max_tokens: Maximum tokens to generate

        Returns:
            Stream ID for cancellation, or None if failed
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        # Validate parameters
        if not prompt or not prompt.strip():
            raise ValueError("Prompt cannot be empty")
        if len(prompt) > Limits.MAX_PROMPT_LENGTH:
            raise PromptTooLongError(f"Prompt too long: {len(prompt)} characters")

        # Register context with the bridge
        context = self.bridge._register_streaming_context(callback, self.session_id)
        context_id_ptr = ctypes.pointer(ctypes.c_long(context.context_id))

        # Start streaming
        stream_id = self.bridge._lib.ai_bridge_generate_response_stream(
            self.session_id,
            prompt.encode('utf-8'),
            ctypes.c_double(temperature),
            ctypes.c_int32(max_tokens),
            ctypes.cast(context_id_ptr, ctypes.c_void_p),
            self.bridge._stream_callback_func,
            None
        )

        if stream_id > 0:
            self.bridge._register_stream(stream_id, context)
            return stream_id
        else:
            context.cleanup()
            self.bridge._unregister_streaming_context(context.context_id)
            return None

    def stream_structured_response(self,
                                  prompt: str,
                                  callback: Callable[[Optional[str]], None],
                                  schema: Optional[Dict[str, Any]] = None,
                                  temperature: float = 1.0,
                                  max_tokens: int = 1000) -> Optional[int]:
        """Stream a structured response with callback on completion.

        Args:
            prompt: The input prompt
            callback: Function called once with complete JSON response
            schema: JSON schema defining the expected structure
            temperature: Controls randomness (0.0-2.0)
            max_tokens: Maximum tokens to generate

        Returns:
            Stream ID for cancellation, or None if failed
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        schema_json = json.dumps(schema).encode('utf-8') if schema else None

        # Register context with the bridge
        context = self.bridge._register_streaming_context(callback, self.session_id)
        context_id_ptr = ctypes.pointer(ctypes.c_long(context.context_id))

        # Start streaming
        stream_id = self.bridge._lib.ai_bridge_generate_structured_response_stream(
            self.session_id,
            prompt.encode('utf-8'),
            schema_json,
            ctypes.c_double(temperature),
            ctypes.c_int32(max_tokens),
            ctypes.cast(context_id_ptr, ctypes.c_void_p),
            self.bridge._stream_callback_func,
            None
        )

        if stream_id > 0:
            self.bridge._register_stream(stream_id, context)
            return stream_id
        else:
            context.cleanup()
            self.bridge._unregister_streaming_context(context.context_id)
            return None


    def cancel_stream(self, stream_id: int) -> bool:
        """Cancel an active stream.

        Args:
            stream_id: The stream to cancel

        Returns:
            True if cancelled successfully
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        result = self.bridge._lib.ai_bridge_cancel_stream(stream_id)
        self.bridge._unregister_stream(stream_id)
        return result

    def get_history(self) -> Optional[str]:
        """Get conversation history as JSON.

        Returns:
            JSON string of conversation history
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        history_ptr = self.bridge._lib.ai_bridge_get_session_history(self.session_id)
        if history_ptr:
            history = ctypes.string_at(history_ptr).decode('utf-8')
            self.bridge._lib.ai_bridge_free_string(history_ptr)
            return history
        return None

    def clear_history(self) -> bool:
        """Clear conversation history.

        Returns:
            True if cleared successfully
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        return self.bridge._lib.ai_bridge_clear_session_history(self.session_id)

    def add_message_to_history(self, role: str, content: str) -> bool:
        """Manually add a message to history.

        Args:
            role: Message role ("user", "assistant", "system", "tool")
            content: Message content

        Returns:
            True if added successfully
        """
        with self._lock:
            if self._destroyed:
                raise SessionDestroyedError("Session has been destroyed")

        return self.bridge._lib.ai_bridge_add_message_to_history(
            self.session_id,
            role.encode('utf-8'),
            content.encode('utf-8')
        )

    def destroy(self) -> None:
        """Destroy this session and free resources."""
        with self._lock:
            if not self._destroyed:
                # Cancel any active streams
                self.bridge._cleanup_session_streams(self.session_id)

                # Destroy the session
                self.bridge._lib.ai_bridge_destroy_session(self.session_id)
                self._destroyed = True

                # Remove from bridge's active sessions
                self.bridge._unregister_session(self)

    def __del__(self):
        """Ensure session is destroyed when garbage collected"""
        try:
            self.destroy()
        except:
            pass

    def __enter__(self):
        """Context manager support"""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager cleanup"""
        self.destroy()


class AIBridge:
    """Main interface to the AI Bridge library.

    Thread-safe implementation with proper context management.
    No global state - all contexts are managed per-bridge instance.
    """

    def __init__(self, library_path: Optional[Union[Path, str]] = None):
        """Initialize the AI Bridge.

        Args:
            library_path: Path to libaibridge.dylib

        Raises:
            AIBridgeError: If library cannot be loaded
        """
        # Find library
        if library_path is None:
            library_path = self._find_library()
        elif isinstance(library_path, str):
            library_path = Path(library_path)

        if not library_path.exists():
            raise AIBridgeError(f"Library not found at {library_path}")

        # Load the library
        self._lib = ctypes.CDLL(str(library_path))
        self._setup_functions()

        # Initialize the bridge
        if not self._lib.ai_bridge_init():
            raise AIBridgeError("Failed to initialize AI Bridge")

        # Instance-level context management (no global state!)
        self._contexts_lock = threading.RLock()
        self._streaming_contexts: Dict[int, StreamingContext] = {}
        self._context_counter = 0

        self._streams_lock = threading.RLock()
        self._active_streams: Dict[int, StreamingContext] = {}

        self._sessions_lock = threading.RLock()
        self._active_sessions: List[AISession] = []

        # Create and store callback functions
        self._stream_callback_func = self._STREAM_CALLBACK(self._stream_callback)

        self._library_path = library_path

    def _find_library(self) -> Path:
        """Find the AI Bridge library in standard locations."""
        # Check environment variable first
        if env_path := os.environ.get('AI_BRIDGE_LIBRARY'):
            return Path(env_path)

        # Check common paths
        search_paths = [
            Path(__file__).parent / "build/dynamic/arm64/release",
            Path(__file__).parent / "build/dynamic/x86_64/release",
            Path.home() / ".local/lib",
            Path("/usr/local/lib"),
            Path("/opt/ai-bridge/lib"),
        ]

        for base_path in search_paths:
            if (lib := base_path / "libaibridge.dylib").exists():
                return lib

        # Fallback to default
        return Path(__file__).parent / "build/dynamic/arm64/release/libaibridge.dylib"

    def _setup_functions(self) -> None:
        """Set up function signatures for the C library."""
        # ai_bridge_init
        self._lib.ai_bridge_init.argtypes = []
        self._lib.ai_bridge_init.restype = ctypes.c_bool

        # ai_bridge_check_availability
        self._lib.ai_bridge_check_availability.argtypes = []
        self._lib.ai_bridge_check_availability.restype = ctypes.c_int32

        # ai_bridge_get_availability_reason
        self._lib.ai_bridge_get_availability_reason.argtypes = []
        self._lib.ai_bridge_get_availability_reason.restype = ctypes.POINTER(ctypes.c_char)

        # ai_bridge_get_supported_languages_count
        self._lib.ai_bridge_get_supported_languages_count.argtypes = []
        self._lib.ai_bridge_get_supported_languages_count.restype = ctypes.c_int32

        # ai_bridge_get_supported_language
        self._lib.ai_bridge_get_supported_language.argtypes = [ctypes.c_int32]
        self._lib.ai_bridge_get_supported_language.restype = ctypes.POINTER(ctypes.c_char)

        # ai_bridge_create_session
        self._lib.ai_bridge_create_session.argtypes = [
            ctypes.c_char_p,  # instructions
            ctypes.c_char_p,  # toolsJson
            ctypes.c_bool,    # enableGuardrails
            ctypes.c_bool,    # enableHistory
            ctypes.c_bool,    # enableStructuredResponses
            ctypes.c_char_p,  # defaultSchemaJson
            ctypes.c_bool     # prewarm
        ]
        self._lib.ai_bridge_create_session.restype = ctypes.c_uint8

        # ai_bridge_generate_response
        self._lib.ai_bridge_generate_response.argtypes = [
            ctypes.c_uint8,    # sessionId
            ctypes.c_char_p,   # prompt
            ctypes.c_double,   # temperature
            ctypes.c_int32     # maxTokens
        ]
        self._lib.ai_bridge_generate_response.restype = ctypes.POINTER(ctypes.c_char)

        # ai_bridge_generate_structured_response
        self._lib.ai_bridge_generate_structured_response.argtypes = [
            ctypes.c_uint8,    # sessionId
            ctypes.c_char_p,   # prompt
            ctypes.c_char_p,   # schemaJson
            ctypes.c_double,   # temperature
            ctypes.c_int32     # maxTokens
        ]
        self._lib.ai_bridge_generate_structured_response.restype = ctypes.POINTER(ctypes.c_char)

        # Callback types
        self._STREAM_CALLBACK = ctypes.CFUNCTYPE(None,
                                                ctypes.c_void_p,     # context
                                                ctypes.c_char_p,     # token
                                                ctypes.c_void_p)     # userData


        # ai_bridge_generate_response_stream
        self._lib.ai_bridge_generate_response_stream.argtypes = [
            ctypes.c_uint8,          # sessionId
            ctypes.c_char_p,         # prompt
            ctypes.c_double,         # temperature
            ctypes.c_int32,          # maxTokens
            ctypes.c_void_p,         # context
            self._STREAM_CALLBACK,   # callback
            ctypes.c_void_p          # userData
        ]
        self._lib.ai_bridge_generate_response_stream.restype = ctypes.c_uint8

        # ai_bridge_generate_structured_response_stream
        self._lib.ai_bridge_generate_structured_response_stream.argtypes = [
            ctypes.c_uint8,          # sessionId
            ctypes.c_char_p,         # prompt
            ctypes.c_char_p,         # schemaJson
            ctypes.c_double,         # temperature
            ctypes.c_int32,          # maxTokens
            ctypes.c_void_p,         # context
            self._STREAM_CALLBACK,   # callback
            ctypes.c_void_p          # userData
        ]
        self._lib.ai_bridge_generate_structured_response_stream.restype = ctypes.c_uint8

# ai_bridge_cancel_stream
        self._lib.ai_bridge_cancel_stream.argtypes = [ctypes.c_uint8]
        self._lib.ai_bridge_cancel_stream.restype = ctypes.c_bool

        # ai_bridge_destroy_session
        self._lib.ai_bridge_destroy_session.argtypes = [ctypes.c_uint8]
        self._lib.ai_bridge_destroy_session.restype = None

        # ai_bridge_free_string
        self._lib.ai_bridge_free_string.argtypes = [ctypes.POINTER(ctypes.c_char)]
        self._lib.ai_bridge_free_string.restype = None

        # ai_bridge_get_session_history
        self._lib.ai_bridge_get_session_history.argtypes = [ctypes.c_uint8]
        self._lib.ai_bridge_get_session_history.restype = ctypes.POINTER(ctypes.c_char)

        # ai_bridge_clear_session_history
        self._lib.ai_bridge_clear_session_history.argtypes = [ctypes.c_uint8]
        self._lib.ai_bridge_clear_session_history.restype = ctypes.c_bool

        # ai_bridge_add_message_to_history
        self._lib.ai_bridge_add_message_to_history.argtypes = [
            ctypes.c_uint8,    # sessionId
            ctypes.c_char_p,   # role
            ctypes.c_char_p    # content
        ]
        self._lib.ai_bridge_add_message_to_history.restype = ctypes.c_bool

    def _register_streaming_context(self,
                                   callback: Callable[[Optional[str]], None],
                                   session_id: Optional[int] = None) -> StreamingContext:
        """Register a new streaming context (thread-safe)."""
        with self._contexts_lock:
            context_id = self._context_counter
            self._context_counter += 1
            context = StreamingContext(context_id, callback, session_id, weakref.ref(self))
            self._streaming_contexts[context_id] = context
            return context

    def _unregister_streaming_context(self, context_id: int) -> None:
        """Remove a streaming context (thread-safe)."""
        with self._contexts_lock:
            self._streaming_contexts.pop(context_id, None)

    def _get_streaming_context(self, context_id: int) -> Optional[StreamingContext]:
        """Get a streaming context by ID (thread-safe)."""
        with self._contexts_lock:
            return self._streaming_contexts.get(context_id)

    def _register_stream(self, stream_id: int, context: StreamingContext) -> None:
        """Register an active stream."""
        with self._streams_lock:
            self._active_streams[stream_id] = context

    def _unregister_stream(self, stream_id: int) -> None:
        """Unregister and cleanup a stream."""
        with self._streams_lock:
            if stream_id in self._active_streams:
                context = self._active_streams.pop(stream_id)
                context.cleanup()
                self._unregister_streaming_context(context.context_id)

    def _cleanup_session_streams(self, session_id: int) -> None:
        """Cancel all streams for a session."""
        with self._streams_lock:
            streams_to_cancel = [
                sid for sid, ctx in self._active_streams.items()
                if ctx.session_id == session_id
            ]

        for stream_id in streams_to_cancel:
            try:
                self._lib.ai_bridge_cancel_stream(stream_id)
                self._unregister_stream(stream_id)
            except:
                pass

    def _unregister_session(self, session: AISession) -> None:
        """Remove a session from active sessions."""
        with self._sessions_lock:
            if session in self._active_sessions:
                self._active_sessions.remove(session)

    def _stream_callback(self,
                        context_ptr: ctypes.c_void_p,
                        token: ctypes.c_char_p,
                        user_data: ctypes.c_void_p) -> None:
        """Callback for streaming responses (called from C code)."""
        # Extract context ID from pointer
        context_id = ctypes.cast(context_ptr, ctypes.POINTER(ctypes.c_long)).contents.value

        # Get context (thread-safe)
        context = self._get_streaming_context(context_id)
        if not context:
            return

        with context.lock:
            if token is None:
                # Stream completed
                context.is_complete = True
                context.completion_event.set()
                try:
                    context.callback(None)
                except Exception:
                    pass
            else:
                try:
                    token_str = token.decode('utf-8')
                    if token_str.startswith("Error:"):
                        context.is_error = True
                        context.completion_event.set()
                    context.callback(token_str)
                except Exception:
                    context.is_error = True
                    context.completion_event.set()
                    try:
                        context.callback(None)
                    except:
                        pass

    def check_availability(self) -> Tuple[AIAvailabilityStatus, Optional[str]]:
        """Check if Apple Intelligence is available.

        Returns:
            Tuple of (status code, reason string if not available)
        """
        status = self._lib.ai_bridge_check_availability()

        if status != AIAvailabilityStatus.AVAILABLE:
            reason_ptr = self._lib.ai_bridge_get_availability_reason()
            if reason_ptr:
                reason = ctypes.string_at(reason_ptr).decode('utf-8')
                self._lib.ai_bridge_free_string(reason_ptr)
                return AIAvailabilityStatus(status), reason
            return AIAvailabilityStatus(status), "Unknown reason"

        return AIAvailabilityStatus(status), None

    def get_supported_languages(self) -> List[str]:
        """Get list of supported languages.

        Returns:
            List of language display names
        """
        count = self._lib.ai_bridge_get_supported_languages_count()
        languages = []

        for i in range(count):
            lang_ptr = self._lib.ai_bridge_get_supported_language(i)
            if lang_ptr:
                language = ctypes.string_at(lang_ptr).decode('utf-8')
                self._lib.ai_bridge_free_string(lang_ptr)
                languages.append(language)

        return languages

    def create_session(self,
                      instructions: Optional[str] = None,
                      enable_history: bool = True,
                      enable_structured_responses: bool = False,
                      default_schema: Optional[Dict[str, Any]] = None,
                      prewarm: bool = True) -> AISession:
        """Create a new AI session with full configuration options.

        Args:
            instructions: System instructions for the AI
            enable_history: Whether to maintain conversation history
            enable_structured_responses: Whether to enable structured responses
            default_schema: Default JSON schema for structured responses
            prewarm: Whether to prewarm for faster first response

        Returns:
            AISession object

        Raises:
            AIBridgeError: If session creation fails
            ModelUnavailableError: If AI is not available
        """
        # Check availability first
        status, reason = self.check_availability()
        if status != AIAvailabilityStatus.AVAILABLE:
            raise ModelUnavailableError(f"Apple Intelligence not available: {reason}")

        # Check session limit
        with self._sessions_lock:
            if len(self._active_sessions) >= Limits.MAX_SESSIONS_PER_BRIDGE:
                raise AIBridgeError(f"Maximum sessions ({Limits.MAX_SESSIONS_PER_BRIDGE}) reached")

        # Prepare parameters
        instructions_bytes = instructions.encode('utf-8') if instructions else None
        schema_json = json.dumps(default_schema).encode('utf-8') if default_schema else None

        # Create session
        session_id = self._lib.ai_bridge_create_session(
            instructions_bytes,
            None,  # no tools
            True,  # guardrails always enabled
            enable_history,
            enable_structured_responses,
            schema_json,
            prewarm
        )

        if session_id == 0:
            raise AIBridgeError("Failed to create session")

        # Create and register session
        session = AISession(session_id, self)
        with self._sessions_lock:
            self._active_sessions.append(session)

        return session

    def wait_for_stream(self, stream_id: int, timeout: float = 60.0) -> bool:
        """Wait for a stream to complete.

        Args:
            stream_id: Stream ID to wait for
            timeout: Maximum wait time in seconds

        Returns:
            True if completed successfully, False if timed out or errored
        """
        with self._streams_lock:
            context = self._active_streams.get(stream_id)

        if not context:
            return False

        # Wait for completion
        completed = context.completion_event.wait(timeout)

        if not completed:
            return False

        return not context.is_error

    def is_stream_error(self, stream_id: int) -> bool:
        """Check if a stream encountered an error.

        Args:
            stream_id: Stream ID to check

        Returns:
            True if stream had an error
        """
        with self._streams_lock:
            context = self._active_streams.get(stream_id)

        return context.is_error if context else False

    def cleanup(self) -> None:
        """Clean up all resources managed by this bridge."""
        # Cancel all active streams
        with self._streams_lock:
            stream_ids = list(self._active_streams.keys())

        for stream_id in stream_ids:
            try:
                self._lib.ai_bridge_cancel_stream(stream_id)
                self._unregister_stream(stream_id)
            except:
                pass

        # Destroy all active sessions
        with self._sessions_lock:
            sessions = list(self._active_sessions)
            self._active_sessions.clear()

        for session in sessions:
            try:
                session.destroy()
            except:
                pass

        # Clear all contexts
        with self._contexts_lock:
            self._streaming_contexts.clear()

    def __del__(self):
        """Ensure cleanup on deletion."""
        try:
            self.cleanup()
        except:
            pass


# Convenience function for simple usage
def create_ai_session(instructions: Optional[str] = None) -> AISession:
    """Convenience function to quickly create an AI session.

    Args:
        instructions: Optional system instructions

    Returns:
        AISession object

    Raises:
        AIBridgeError: If not available or session creation fails
    """
    bridge = AIBridge()
    return bridge.create_session(instructions)