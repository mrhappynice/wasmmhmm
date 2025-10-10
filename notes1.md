Write Once Run Many
---

# 0) What we’re building

* A tiny **standalone process**: `wasmtime serve app.wasm --addr 0.0.0.0:8080`
* Optional front proxy: **Nginx** for TLS, gzip, caching
* Same binary works on Linux, macOS, Windows

Wasmtime is battle-tested, cross-platform, supports the **WASI-HTTP** API (so your Wasm can be an HTTP server without raw sockets), and ships a `serve` subcommand that wires HTTP to your Wasm component. ([docs.wasmtime.dev][1])

---

# 1) Install Wasmtime (one small binary)

**Linux/macOS**

```bash
curl https://wasmtime.dev/install.sh -sSf | bash
# restart your shell or add ~/.wasmtime/bin to PATH
wasmtime -V
```

**Windows**

* Download the MSI from the releases page and install; then `wasmtime -V` in PowerShell. ([docs.wasmtime.dev][2])

---

# 2) Quick sanity check

Fetch a tiny Wasm module (or use one you built) and run:

```bash
wasmtime run your_module.wasm
```

For **HTTP servers**, you’ll instead run:

```bash
wasmtime serve your_http_component.wasm --addr 0.0.0.0:8080
```

The `serve` command expects a component in the **WASI-HTTP** world and handles HTTP for you. ([docs.wasmtime.dev][1])

---

# 3) The fastest route to an HTTP server (WASI-HTTP)

If you just want “hello HTTP” in Wasm, **follow this minimal tutorial** which uses `wasmtime serve` and a tiny WIT world. It shows how to build a component that responds to requests (no raw sockets, just the WASI-HTTP interface): ([GitHub][3])

* `hello-wasi-http` tutorial: shows a component and the exact `wasmtime serve` incantation.
* WASI-HTTP spec (what that interface is): ([GitHub][4])

> You can start with “just run a `.wasm`” using `wasmtime run`, and graduate to `wasmtime serve` once you implement the WASI-HTTP world (examples below).

---

# 4) Production-ish: systemd + Nginx (optional but nice)

**systemd unit (Linux):** `/etc/systemd/system/mywasm.service`

```ini
[Unit]
Description=My WASI-HTTP service
After=network.target

[Service]
ExecStart=/home/ubuntu/.wasmtime/bin/wasmtime serve /opt/app/app.wasm --addr 127.0.0.1:8080
Restart=always
User=www-data
WorkingDirectory=/opt/app
Environment=RUST_LOG=info

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mywasm
```

**Nginx reverse proxy (terminates TLS, serves /static):**

```nginx
server {
  listen 80;
  server_name your.domain;
  location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $remote_addr;
  }
  location /static/ {
    root /var/www;
  }
}
```

---

# 5) Language-by-language: compile to Wasm (minimal examples)

> Tip: There are **two common targets**:
>
> * `wasm32-wasi` → runs under Wasmtime/servers; can use WASI APIs (files, clocks, env, etc.).
> * `wasm32-unknown-unknown` → browser use; no WASI, loaded by JS `WebAssembly.instantiateStreaming()`.

Below I’ll show “hello” that runs with **Wasmtime** (WASI). For *browser* use, I add a brief note.

---

## A) “JavaScript” → WebAssembly: use **AssemblyScript** (TypeScript-like)

You can’t compile arbitrary JS to Wasm; the battle-tested path is **AssemblyScript**, a TypeScript subset that compiles to Wasm.

**Setup**

```bash
mkdir as-hello && cd as-hello
npm init -y
npm i -D assemblyscript
npx asinit .
```

This scaffolds `assembly/` and `build/`.

**Edit** `assembly/index.ts`

```ts
export function add(a: i32, b: i32): i32 { return a + b; }
```

**Build**

```bash
npm run asbuild
# outputs build/release.wasm
```

Run with Wasmtime:

```bash
wasmtime run build/release.wasm --invoke add 2 40
# => 42
```

AssemblyScript quickstart docs here. For browsers, serve `build/release.wasm` and load with JS `WebAssembly.instantiateStreaming()`. ([AssemblyScript][5])

---

## B) Rust → Wasm (WASI)

**Install target**

```bash
rustup target add wasm32-wasi
```

**Hello (stdin/stdout)**

```bash
cargo new --bin rust-wasi && cd rust-wasi
```

`src/main.rs`

```rust
fn main() {
    println!("Hello from Rust+WASI!");
}
```

**Build & run**

```bash
cargo build --release --target wasm32-wasi
wasmtime run target/wasm32-wasi/release/rust-wasi.wasm
```

**HTTP (WASI-HTTP) path:** follow the “hello-wasi-http” tutorial to produce a component that `wasmtime serve` can run as an HTTP server. (It shows the WIT world and a minimal handler.)

---

## C) Python → Wasm (WASI component) with **componentize-py**

You can’t natively “compile Python like C,” but **componentize-py** packages your Python app (CPython + deps) as a **Wasm component** that runs on Wasmtime.

**Install**

```bash
python -m pip install componentize-py
```

**Define a WIT world** `adder.wit`

```wit
package demo:adder@0.1.0;

world adder {
  export add: func(a: s32, b: s32) -> s32
}
```

**Python implementation** `app.py`

```python
# componentize-py will generate bindings; for demo, just implement add()
def add(a: int, b: int) -> int:
    return a + b
```

**Build component**

```bash
componentize-py -w adder.wit -n demo:adder/adder -m app -o adder.component.wasm
```

**Run**

```bash
wasmtime run adder.component.wasm --invoke add 20 22
# => 42
```

Docs & background on componentize-py and Python support for the Component Model: ([component-model.bytecodealliance.org][6])

---

## D) Go → Wasm (WASI) with **TinyGo**

Stock Go’s Wasm target is for the browser; for **WASI** you want **TinyGo** (very small output, works great with Wasmtime).

**Install TinyGo** (per OS instructions), then:

```bash
mkdir tinygo-wasi && cd tinygo-wasi
go mod init example.com/tiny
cat > main.go << 'EOF'
package main
import "fmt"
func main() { fmt.Println("Hello from TinyGo+WASI!") }
EOF

tinygo build -o app.wasm -target=wasi main.go
wasmtime run app.wasm
```

Fermyon’s guide is a nice reference for TinyGo→WASI patterns. For HTTP specifically, you can pair TinyGo guest libs with a WASI-HTTP host (`wasmtime serve`) or use an HTTP guest ABI like http-wasm where appropriate. ([Fermyon Developer][7])

---

## E) C → Wasm (WASI) with **Clang** or **wasi-sdk**

**Option 1: wasi-sdk (easiest)**

```bash
# write hello.c
cat > hello.c << 'EOF'
#include <stdio.h>
int main(void) { puts("Hello from C+WASI!"); return 0; }
EOF

# assuming wasi-sdk/bin on PATH (or use absolute path)
clang --target=wasm32-wasi hello.c -O2 -o hello.wasm
wasmtime run hello.wasm
```

**Option 2: Clang 17 with a WASI sysroot**
Use `-target wasm32-wasi` and point to a WASI sysroot; see concrete commands in this write-up and the wasi-sdk README. ([danielmangum.com][8])

---

# 6) Turning any of the above into an HTTP service

To use `wasmtime serve`, your Wasm must be a **component** implementing the **WASI-HTTP** world (not just a plain `main()`). The “hello-wasi-http” tutorial shows a working example and how to run it:

```bash
wasmtime serve your_http_component.wasm --addr 0.0.0.0:8080
```

Under the hood, the runtime hosts the **WASI-HTTP** interfaces so your component can handle requests/responses. Wasmtime’s WASI-HTTP host implementation is built on `hyper`/`tokio`, and the `serve` CLI is officially documented. ([docs.wasmtime.dev][9])

> If you already have an existing REST service and just want to “try Wasm,” keep your current web server (Nginx, Caddy, or whatever) and call into a Wasm component for pure compute. Move to `wasmtime serve` once you adopt the WASI-HTTP world.

---

# 7) Browser usage (the “web” part)

For **browser** execution, compile to `wasm32-unknown-unknown` (Rust) or use AssemblyScript (or C via Emscripten). Load in JS:

```html
<script type="module">
  const response = await fetch('/app.wasm');
  const { instance } = await WebAssembly.instantiateStreaming(response, {});
  console.log(instance.exports.add(2, 40));
</script>
```

The **Component Model** improves interop between Wasm modules and hosts (including browsers-in-the-future and servers-now). If you’re curious how components compose and why this matters, this NGINX Unit explainer is good background. ([unit.nginx.org][10])

---

# 8) Cheatsheet / what to pick

* **Smallest install, runs everywhere:** Wasmtime CLI. ([wasmtime.dev][11])
* **Plain compute:** `wasmtime run foo.wasm`
* **HTTP server:** implement WASI-HTTP world → `wasmtime serve foo.wasm --addr :8080` ([docs.wasmtime.dev][1])
* **Reverse proxy & TLS:** Nginx (tiny config as above)
* **Languages:**

  * JS-ish → **AssemblyScript** (battle-tested path to Wasm) ([AssemblyScript][5])
  * Rust → `wasm32-wasi` (first-class support)
  * Python → **componentize-py** (packages CPython + your code as a Wasm component) ([component-model.bytecodealliance.org][6])
  * Go → **TinyGo** target `wasi` (small binaries) ([Fermyon Developer][7])
  * C → **wasi-sdk** or **Clang 17** with `-target wasm32-wasi` ([GitHub][12])


[1]: https://docs.wasmtime.dev/cli-options.html?utm_source=chatgpt.com "CLI Options"
[2]: https://docs.wasmtime.dev/cli-install.html?utm_source=chatgpt.com "Installation - Wasmtime"
[3]: https://github.com/sunfishcode/hello-wasi-http?utm_source=chatgpt.com "sunfishcode/hello-wasi-http"
[4]: https://github.com/WebAssembly/wasi-http?utm_source=chatgpt.com "WebAssembly/wasi-http: A collection of interfaces ..."
[5]: https://www.assemblyscript.org/getting-started.html?utm_source=chatgpt.com "Getting started | The AssemblyScript Book"
[6]: https://component-model.bytecodealliance.org/language-support/python.html?utm_source=chatgpt.com "Python - The WebAssembly Component Model"
[7]: https://developer.fermyon.com/wasm-languages/go-lang?utm_source=chatgpt.com "Go in WebAssembly - Developer - Fermyon"
[8]: https://danielmangum.com/posts/wasm-wasi-clang-17/?utm_source=chatgpt.com "Zero to WASI with Clang 17"
[9]: https://docs.wasmtime.dev/api/wasmtime_wasi_http/index.html?utm_source=chatgpt.com "wasmtime_wasi_http - Rust"
[10]: https://unit.nginx.org/news/2024/wasm-component-model-part-1/?utm_source=chatgpt.com "The WebAssembly Component Model - The Why, How and ..."
[11]: https://wasmtime.dev/?utm_source=chatgpt.com "Wasmtime"
[12]: https://github.com/WebAssembly/wasi-sdk?utm_source=chatgpt.com "WebAssembly/wasi-sdk"
