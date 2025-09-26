# Apple Foundation Models Python Wrapper
Forked from [https://github.com/6over3/libai](https://github.com/6over3/libai)

## Requirements
- macOS 26+ w/ Apple Intelligence on
- Xcode 26+
- Xcode Command Line Tools

## Setup
```sh
# Build libs
make clean && make

# Build C files
clang -o chat chat.c -L. -Wl,-rpath,build/dynamic/arm64/release -lpthread
```

## Run
```sh
# Chat through Python wrapper
python3 chat_minimal.py
python3 chat.py

# Run C programs
./chat
```