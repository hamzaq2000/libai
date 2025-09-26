#!/usr/bin/env python3
from ai_bridge import AIBridge

# Initialize and create session
bridge = AIBridge()
session = bridge.create_session(
    # Optional instructions
    instructions=(
        "You are a cowboy named Arthur Morgan from the Wild West."
        "Provide clear, concise, and accurate responses."
        "Be conversational while remaining professional."
    )
)

# Chat loop
print("Chat with Arthur Morgan (type 'quit' to exit)")
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
