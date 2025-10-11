If you want “simple + lowest server resources,” the cleanest path is:

**Serve static files with Nginx and run your Rust in the browser as WebAssembly.**
No server-side runtime (no Wasmtime/Bun needed) unless you truly need server compute. It’s the easiest to deploy and uses practically zero CPU/RAM on your server—your users’ browsers do the work.

Below is a complete, minimal-but-real example you can copy. It exposes a couple of Rust functions to JS (including a tiny CPU task to show the speed benefit), builds to WASM, and loads from a plain HTML page you can host on Nginx.

---

# 1) Project layout

```
wasm-demo/
├─ Cargo.toml
├─ src/
│  └─ lib.rs
├─ web/
│  ├─ index.html
│  └─ main.js
```

---

# 2) Rust → WASM (no frameworks)

## `Cargo.toml`

```toml
[package]
name = "wasm_demo"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]   # needed for wasm

[dependencies]
wasm-bindgen = "0.2"
console_error_panic_hook = "0.1"

[profile.release]
opt-level = "z"           # small binary
lto = true
codegen-units = 1
```

## `src/lib.rs`

```rust
use wasm_bindgen::prelude::*;

#[wasm_bindgen(start)]
pub fn start() {
    // Better error messages if something panics
    console_error_panic_hook::set_once();
}

#[wasm_bindgen]
pub fn greet(name: &str) -> String {
    format!("hello, {name} 👋 (from Rust/WASM)")
}

// Simple CPU task to demonstrate speed vs JS: count primes up to n (inclusive).
#[wasm_bindgen]
pub fn count_primes(n: u32) -> u32 {
    if n < 2 { return 0; }
    let size = (n + 1) as usize;
    let mut is_prime = vec![true; size];
    is_prime[0] = false; is_prime[1] = false;
    let mut p = 2usize;
    while p * p <= n as usize {
        if is_prime[p] {
            let mut m = p * p;
            while m <= n as usize {
                is_prime[m] = false;
                m += p;
            }
        }
        p += 1;
    }
    is_prime.into_iter().filter(|&b| b).count() as u32
}
```

---

# 3) Build steps (pick one)

### Option A: **wasm-pack** (easiest)

```bash
# one-time
cargo install wasm-pack

# build for browsers (ES modules)
wasm-pack build --release --target web
```

This creates `pkg/` with `wasm_demo_bg.wasm` and a small JS loader.

You’ll end up with:

```
wasm-demo/
├─ pkg/
│  ├─ wasm_demo.js
│  ├─ wasm_demo_bg.wasm
│  └─ wasm_demo_bg.wasm.d.ts
```

### Option B: `wasm-bindgen-cli` directly

```bash
rustup target add wasm32-unknown-unknown
cargo build --release --target wasm32-unknown-unknown
cargo install wasm-bindgen-cli
wasm-bindgen target/wasm32-unknown-unknown/release/wasm_demo.wasm \
  --out-dir pkg --target web
```

Either way, you’ll have a `pkg/` directory to serve.

---

# 4) Frontend (no bundler)

## `web/index.html`

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <title>Rust → WebAssembly Demo</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body { font-family: system-ui, sans-serif; margin: 2rem; }
    input, button { font-size: 1rem; }
    .row { margin: .5rem 0; }
    code { background: #f5f5f5; padding: .2rem .4rem; }
  </style>
</head>
<body>
  <h1>Rust → WASM (no frameworks)</h1>

  <div class="row">
    <input id="name" placeholder="Your name" />
    <button id="hello">Greet</button>
    <div id="greeting"></div>
  </div>

  <div class="row">
    <input id="limit" type="number" value="100000" />
    <button id="count">Count primes ≤ n</button>
    <div>Result: <span id="result">—</span></div>
    <div><small>(Try 1000000 for a stronger test)</small></div>
  </div>

  <script type="module" src="./main.js"></script>
</body>
</html>
```

## `web/main.js`

```js
// Import the generated JS from wasm-pack (served from ../pkg)
import init, { greet, count_primes } from "../pkg/wasm_demo.js";

await init(); // loads the .wasm next to wasm_demo.js

document.getElementById("hello").addEventListener("click", () => {
  const name = document.getElementById("name").value || "world";
  document.getElementById("greeting").textContent = greet(name);
});

document.getElementById("count").addEventListener("click", () => {
  const n = parseInt(document.getElementById("limit").value, 10) || 0;
  const t0 = performance.now();
  const c = count_primes(n);
  const t1 = performance.now();
  document.getElementById("result").textContent =
    `${c} primes (computed in ${(t1 - t0).toFixed(1)} ms)`;
});
```

> To run locally without Nginx, use any static server (e.g., `python3 -m http.server`) from the project root and open `/web/`.

---

# 5) Nginx config (serve static + correct MIME)

Add (or ensure) the WASM MIME type and allow caching/HTTP/2:

```nginx
http {
  types {
    application/wasm wasm;
  }
  # ...
}

server {
  listen 80;              # or 443 ssl http2;
  server_name example.com;

  root /var/www/wasm-demo;  # point to project root (contains web/ and pkg/)
  index web/index.html;

  location / {
    try_files $uri $uri/ /web/index.html;
  }

  # Cache WASM and JS aggressively (tune for your needs)
  location ~* \.(wasm|js)$ {
    add_header Cache-Control "public, max-age=31536000, immutable";
    try_files $uri =404;
  }
}
```

Deploy by copying the generated `pkg/` and the `web/` directory to your doc root:

```
/var/www/wasm-demo/
├─ pkg/
└─ web/
```

---

# 6) Why this is the best “simple + low resources” balance

* **Zero server runtime.** Nginx just serves files. No Wasmtime, no Node/Bun process to babysit. RAM/CPU on the server stay tiny.
* **Modern browser-native.** `wasm-bindgen` shapes Rust strings/arrays to JS nicely. ES modules load cleanly.
* **Fast cold starts.** The WASM file is small and cached; user devices do the compute.
* **Easy to grow.** When you need heavy rendering, mount a `<canvas>` and render from Rust (via `web-sys`/`wasm-bindgen`) without changing the hosting model.

---

# 7) When to use a server-side WASM runtime (Wasmtime) instead

Choose **Wasmtime** (WASI) only if:

* You need to run untrusted workloads **on your server** with strong sandboxing.
* You have tasks that can’t run in the browser (private data, large memory, long jobs).
* You want the same Rust/WASM binary to run in CLI + server contexts.

Even then, you can keep the same front-end and call a small HTTP endpoint that invokes Wasmtime; your Nginx can reverse-proxy to that service. It’s just more moving parts and more server load.

---

# 8) Tiny extras (optional)

* **File size:** add `-C link-arg=-s` via `.cargo/config.toml` to strip symbols.
* **Web Worker:** If `count_primes` gets big and blocks the UI, load the WASM in a Worker and postMessage the results.
* **TypeScript:** `wasm-pack` emits `.d.ts` so you can get types in TS projects.

---

