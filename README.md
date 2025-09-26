# Apple Foundation Models Python Wrapper
Forked from [https://github.com/6over3/libai](https://github.com/6over3/libai).
```sh
# Build libs
make clean && make

# Chat through Python wrapper
python3 chat_minimal.py
python3 chat.py

# Build C files
clang -o chat chat.c -L. -Wl,-rpath,build/dynamic/arm64/release -lpthread

# Run C programs
./chat
```