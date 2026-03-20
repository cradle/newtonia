# Project Notes

## Build environment

SDL2 headers are not installed. Do not run `make` to verify C++ changes — use a syntax-only check instead:

```bash
g++ -std=c++11 -fsyntax-only -x c++ - <<'EOF'
// minimal includes + code under test
EOF
```
