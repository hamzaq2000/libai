#!/usr/bin/env python3
"""
Interactive chat demo using the AI Bridge wrapper.

This demonstrates best practices for using Apple Intelligence through
the Python wrapper, including streaming responses, structured
generation, and proper resource management.

Usage:
    python3 chat.py

Commands:
    quit/exit - Exit the chat
    /clear - Start a new session
    /history - Show conversation history
    /languages - Show supported languages
    /temp <value> - Set temperature (0.0-2.0)
    /tokens <value> - Set max tokens
    /help - Show available commands
"""

import sys
import json
import time
from pathlib import Path
from typing import Optional, Dict, Any
from ai_bridge import (
    AIBridge,
    AIAvailabilityStatus,
    AIBridgeError,
    SessionDestroyedError,
    PromptTooLongError,
    Limits
)


# Terminal colors for better UX
class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    GREEN = "\033[32m"
    BLUE = "\033[34m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"
    RED = "\033[31m"
    GRAY = "\033[90m"


class ChatSession:
    """Interactive chat session with Apple Intelligence."""

    def __init__(self):
        self.bridge: Optional[AIBridge] = None
        self.session = None
        self.temperature = 1.0
        self.max_tokens = 1000

    def initialize(self) -> bool:
        """Initialize the AI Bridge and check availability."""
        print(f"{Colors.CYAN}🚀 Initializing Apple Intelligence...{Colors.RESET}")

        try:
            self.bridge = AIBridge()
        except AIBridgeError as e:
            print(f"{Colors.RED}❌ Failed to initialize: {e}{Colors.RESET}")
            return False

        # Check availability
        status, reason = self.bridge.check_availability()
        if status != AIAvailabilityStatus.AVAILABLE:
            if status == AIAvailabilityStatus.MODEL_NOT_READY:
                print(f"{Colors.YELLOW}⏳ Model downloading: {reason}{Colors.RESET}")
                print(f"{Colors.YELLOW}   Please wait for download to complete.{Colors.RESET}")
            else:
                print(f"{Colors.RED}❌ Not available: {reason}{Colors.RESET}")
            return False

        print(f"{Colors.GREEN}✅ Apple Intelligence is ready!{Colors.RESET}")

        # Show language support
        languages = self.bridge.get_supported_languages()
        if languages:
            print(f"{Colors.GRAY}Supported languages: {len(languages)}{Colors.RESET}")

        return True

    def create_new_session(self) -> bool:
        """Create a new chat session."""
        if not self.bridge:
            return False

        try:
            # Destroy existing session if any
            if self.session:
                self.session.destroy()

            # Create new session with appropriate settings
            self.session = self.bridge.create_session(
                instructions=(
                    "You are a helpful, friendly, and knowledgeable assistant. "
                    "Provide clear, concise, and accurate responses. "
                    "Be conversational while remaining professional."
                ),
                enable_history=True,
                prewarm=True
            )

            return True

        except AIBridgeError as e:
            print(f"{Colors.RED}Failed to create session: {e}{Colors.RESET}")
            return False

    def stream_callback(self, token: Optional[str]) -> None:
        """Callback for streaming tokens."""
        if token is None:
            # Stream completed
            return
        elif token.startswith("Error:"):
            print(f"{Colors.RED}\n{token}{Colors.RESET}", end='', flush=True)
        else:
            print(token, end='', flush=True)

    def process_message(self, prompt: str) -> None:
        """Process a user message and generate response."""
        if not self.session:
            print(f"{Colors.RED}No active session{Colors.RESET}")
            return

        start_time = time.time()

        try:
            # Example: How to use structured responses if needed
            # ------------------------------------------------
            # To extract specific data like product reviews or technical specs:
            #
            # product_review_schema = {
            #     "type": "object",
            #     "properties": {
            #         "product_name": {"type": "string"},
            #         "rating": {"type": "integer", "minimum": 1, "maximum": 5},
            #         "pros": {
            #             "type": "array",
            #             "items": {"type": "string"},
            #             "maxItems": 5
            #         },
            #         "cons": {
            #             "type": "array",
            #             "items": {"type": "string"},
            #             "maxItems": 5
            #         },
            #         "recommendation": {"type": "boolean"},
            #         "summary": {"type": "string", "maxLength": 200}
            #     },
            #     "required": ["product_name", "rating", "recommendation"]
            # }
            #
            # # Create session with structured responses enabled
            # structured_session = self.bridge.create_session(
            #     enable_structured_responses=True,
            #     enable_history=True
            # )
            #
            # # Generate structured review data
            # result = structured_session.generate_structured_response(
            #     prompt="Review the iPhone 15 Pro based on user feedback",
            #     schema=product_review_schema,
            #     temperature=0.3,  # Lower temperature for consistent structure
            #     max_tokens=500
            # )
            #
            # # Parse the JSON response
            # if isinstance(result, dict) and "object" in result:
            #     review_data = json.loads(result["object"])
            #     print(f"Product: {review_data['product_name']}")
            #     print(f"Rating: {'⭐' * review_data['rating']}")
            #     print(f"Recommended: {'Yes' if review_data['recommendation'] else 'No'}")

            # Stream regular response
            print(f"{Colors.GREEN}AI:{Colors.RESET} ", end='', flush=True)

            stream_id = self.session.stream_response(
                prompt=prompt,
                callback=self.stream_callback,
                temperature=self.temperature,
                max_tokens=self.max_tokens
            )

            if stream_id:
                # Wait for streaming to complete
                success = self.bridge.wait_for_stream(stream_id, timeout=60.0)
                print()  # New line after response

                if not success and self.bridge.is_stream_error(stream_id):
                    print(f"{Colors.RED}Stream encountered an error{Colors.RESET}")
            else:
                print(f"{Colors.RED}Failed to start streaming{Colors.RESET}")

            # Show timing
            elapsed = time.time() - start_time
            print(f"{Colors.GRAY}[{elapsed:.1f}s]{Colors.RESET}")

        except PromptTooLongError as e:
            print(f"{Colors.RED}Prompt too long: {e}{Colors.RESET}")
        except SessionDestroyedError:
            print(f"{Colors.RED}Session was destroyed. Creating new session...{Colors.RESET}")
            if self.create_new_session():
                self.process_message(prompt)  # Retry with new session
        except ValueError as e:
            print(f"{Colors.RED}Invalid input: {e}{Colors.RESET}")
        except AIBridgeError as e:
            print(f"{Colors.RED}Error: {e}{Colors.RESET}")

    def handle_command(self, command: str) -> bool:
        """Handle special commands. Returns False to exit."""
        cmd = command.lower().strip()

        if cmd in ['/quit', '/exit', 'quit', 'exit']:
            return False

        elif cmd == '/clear':
            if self.create_new_session():
                print(f"{Colors.GREEN}✨ Started new conversation{Colors.RESET}")
            else:
                print(f"{Colors.RED}Failed to create new session{Colors.RESET}")

        elif cmd == '/history':
            if self.session:
                history = self.session.get_history()
                if history:
                    try:
                        messages = json.loads(history)
                        print(f"{Colors.CYAN}📜 Conversation History ({len(messages)} messages):{Colors.RESET}")
                        print(f"{Colors.GRAY}{history[:500]}...{Colors.RESET}" if len(history) > 500 else history)
                    except:
                        print(f"{Colors.CYAN}📜 History available ({len(history)} chars){Colors.RESET}")
                else:
                    print(f"{Colors.YELLOW}No history available{Colors.RESET}")

        elif cmd == '/languages':
            if self.bridge:
                languages = self.bridge.get_supported_languages()
                print(f"{Colors.CYAN}🌐 Supported Languages ({len(languages)}):{Colors.RESET}")
                for i, lang in enumerate(languages, 1):
                    print(f"  {i}. {lang}")
                    if i >= 10:  # Show first 10
                        print(f"  ... and {len(languages) - 10} more")
                        break

        elif cmd.startswith('/temp '):
            try:
                temp = float(cmd[6:].strip())
                if Limits.MIN_TEMPERATURE <= temp <= Limits.MAX_TEMPERATURE:
                    self.temperature = temp
                    print(f"{Colors.GREEN}🌡️ Temperature set to {self.temperature:.2f}{Colors.RESET}")
                    print(f"{Colors.GRAY}0.0=deterministic, 1.0=balanced, 2.0=creative{Colors.RESET}")
                else:
                    print(f"{Colors.RED}Temperature must be between {Limits.MIN_TEMPERATURE} and {Limits.MAX_TEMPERATURE}{Colors.RESET}")
            except ValueError:
                print(f"{Colors.RED}Invalid temperature value{Colors.RESET}")

        elif cmd.startswith('/tokens '):
            try:
                tokens = int(cmd[8:].strip())
                if Limits.MIN_TOKENS <= tokens <= Limits.MAX_TOKENS:
                    self.max_tokens = tokens
                    print(f"{Colors.GREEN}📝 Max tokens set to {self.max_tokens}{Colors.RESET}")
                else:
                    print(f"{Colors.RED}Tokens must be between {Limits.MIN_TOKENS} and {Limits.MAX_TOKENS}{Colors.RESET}")
            except ValueError:
                print(f"{Colors.RED}Invalid token count{Colors.RESET}")

        elif cmd == '/help':
            self.show_help()

        else:
            print(f"{Colors.YELLOW}Unknown command. Type /help for available commands.{Colors.RESET}")

        return True

    def show_welcome(self):
        """Display welcome message."""
        print(f"\n{Colors.BOLD}💬 Chat with Apple Intelligence{Colors.RESET}")
        print("=" * 50)
        print(f"Model: Apple Intelligence Foundation Model")
        print(f"Temperature: {self.temperature:.1f} | Max Tokens: {self.max_tokens}")
        print(f"\nType {Colors.CYAN}/help{Colors.RESET} for commands, or start chatting!")
        print("=" * 50)

    def show_help(self):
        """Display help information."""
        print(f"\n{Colors.BOLD}Available Commands:{Colors.RESET}")
        print(f"  {Colors.CYAN}quit/exit{Colors.RESET} - Exit the chat")
        print(f"  {Colors.CYAN}/clear{Colors.RESET} - Start a new conversation")
        print(f"  {Colors.CYAN}/history{Colors.RESET} - Show conversation history")
        print(f"  {Colors.CYAN}/languages{Colors.RESET} - List supported languages")
        print(f"  {Colors.CYAN}/temp <0.0-2.0>{Colors.RESET} - Set temperature (current: {self.temperature:.1f})")
        print(f"  {Colors.CYAN}/tokens <1-100000>{Colors.RESET} - Set max tokens (current: {self.max_tokens})")
        print(f"  {Colors.CYAN}/help{Colors.RESET} - Show this help message")

    def run(self):
        """Main chat loop."""
        if not self.initialize():
            return

        if not self.create_new_session():
            return

        self.show_welcome()

        try:
            while True:
                # Get user input
                print(f"\n{Colors.BLUE}You:{Colors.RESET} ", end='', flush=True)

                try:
                    user_input = input().strip()
                except (EOFError, KeyboardInterrupt):
                    print(f"\n{Colors.YELLOW}👋 Goodbye!{Colors.RESET}")
                    break

                if not user_input:
                    continue

                # Check for commands
                if user_input.startswith('/') or user_input.lower() in ['quit', 'exit']:
                    if not self.handle_command(user_input):
                        print(f"{Colors.YELLOW}👋 Goodbye!{Colors.RESET}")
                        break
                else:
                    # Process as regular message
                    self.process_message(user_input)

        except Exception as e:
            print(f"\n{Colors.RED}Unexpected error: {e}{Colors.RESET}")

        finally:
            # Clean up resources
            print(f"{Colors.GRAY}\nCleaning up...{Colors.RESET}")
            if self.session:
                try:
                    self.session.destroy()
                except:
                    pass
            if self.bridge:
                try:
                    self.bridge.cleanup()
                except:
                    pass


def main():
    """Entry point."""
    try:
        chat = ChatSession()
        chat.run()
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Interrupted{Colors.RESET}")
        sys.exit(0)
    except Exception as e:
        print(f"{Colors.RED}Fatal error: {e}{Colors.RESET}")
        sys.exit(1)


if __name__ == "__main__":
    main()