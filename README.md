# HTGL

HTML/CSS-authored UI for embedded systems. Write screens in an HTML subset,
transpile to a compact binary, render with a tiny portable C engine.

- **Authoring:** an HTML/CSS subset you can preview in any browser
- **Transpiler (PC, Python):** `.html` -> `.uib` binary (+ optional C array)
- **Engine (C99):** loads `.uib`, chunked rendering, HAL-decoupled, no mandatory heap
- **Targets:** low-end MCUs (e.g. STM32F1) up to Linux boards

## Build & run the host simulator

```sh
# transpile the example
cd tool && python htgl.py ../examples/hello.html -o ../build/hello.uib && cd ..
# build engine + simulator
cmake -S . -B build && cmake --build build
# render to PNG
./build/htgl_sim build/hello.uib build/hello.png
```

## Test

```sh
cd tool && python -m pytest          # transpiler
ctest --test-dir build               # engine
```

License: MIT.
