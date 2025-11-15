`Dockerfile.build`

This builds the WASM + JS, lays out a Netlify-ready `dist/` (root contains `index.html`, `main.js`, and `pkg/…`), **and** rewrites the import paths inside `main.js` from `../pkg/...` → `./pkg/...`. It also writes a `netlify.toml` that sets `publish = "dist"` and good caching for `.wasm`/`.js`.

```dockerfile
# Dockerfile.build
FROM debian:stable-slim

# Build dependencies
RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    cmake \
    python3 \
    ca-certificates \
    bash \
 && rm -rf /var/lib/apt/lists/*

# Install Emscripten SDK
RUN git clone https://github.com/emscripten-core/emsdk /emsdk \
 && cd /emsdk \
 && ./emsdk install latest \
 && ./emsdk activate latest

# Use bash so we can `source` emsdk_env.sh
SHELL ["/bin/bash", "-lc"]

WORKDIR /app

# Copy project sources
COPY src ./src
COPY web ./web

# Build C and C++ → WASM + JS into ./pkg
RUN mkdir -p pkg \
 && source /emsdk/emsdk_env.sh \
 && emcc src/c_demo.c -O3 \
      -sENVIRONMENT=web \
      -sEXPORT_ES6=1 -sMODULARIZE=1 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
      -o pkg/c_demo.js \
 && em++ src/cpp_demo.cpp -O3 \
      -sENVIRONMENT=web \
      -sEXPORT_ES6=1 -sMODULARIZE=1 \
      -sALLOW_MEMORY_GROWTH=1 \
      --bind \
      -o pkg/cpp_demo.js

# Create Netlify-ready dist/:
# - copy web/* into dist/
# - rewrite "../pkg/" imports in main.js to "./pkg/"
# - copy pkg/ into dist/pkg/
# - create netlify.toml that publishes "dist" and sets useful caching headers
RUN mkdir -p dist \
 && cp -r web/* dist/ \
 && sed -i 's#\.\./pkg/#./pkg/#g' dist/main.js \
 && mkdir -p dist/pkg \
 && cp -r pkg/* dist/pkg/ \
 && printf '%s\n' \
   '[build]' \
   '  publish = "dist"' \
   '' \
   '# Strong caching for wasm/js (safe because filenames are content-hashed by Emscripten or versioned by deploys)' \
   '[[headers]]' \
   '  for = "/*.wasm"' \
   '  [headers.values]' \
   '    Cache-Control = "public, max-age=31536000, immutable"' \
   '' \
   '[[headers]]' \
   '  for = "/*.js"' \
   '  [headers.values]' \
   '    Cache-Control = "public, max-age=31536000, immutable"' \
   > /app/netlify.toml

# Convenience default
CMD ["bash"]
```

**Why we rewrote the imports:**
Netlify serves from the `publish` directory root. By placing `index.html` and `main.js` at `dist/` and the artifacts at `dist/pkg/`, the browser-visible path from `main.js` is `./pkg/...` (not `../pkg/...`).

---

# 2) (Optional) Keep your `Dockerfile` for local nginx testing

No change needed for Netlify. Netlify will just serve the static `dist/` folder; no runtime container is used there.

---

# 3) Add `deploy_netlify.sh` to the project root

This script:

* Builds the builder image and extracts **`dist/`** + **`netlify.toml`** onto your host
* Uses the Netlify CLI to create the site (if needed) and deploy **to production**
* Accepts a required site name argument: `./deploy_netlify.sh <your-site-name>`

```bash
#!/usr/bin/env bash
set -euo pipefail

# Usage check
if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <netlify-site-name>"
  exit 1
fi

SITE_NAME="$1"

# Require Netlify CLI
if ! command -v netlify >/dev/null 2>&1; then
  echo "Error: Netlify CLI not found. Install with: npm i -g netlify-cli"
  exit 1
fi

# Optional: use token if provided (avoids interactive login)
# export NETLIFY_AUTH_TOKEN=...   # set in your shell or CI

echo "==> Building builder image…"
docker build -f Dockerfile.build -t wasm-proj-build .

echo "==> Creating temp container…"
docker rm -f wasm-proj-build-cont >/dev/null 2>&1 || true
docker create --name wasm-proj-build-cont wasm-proj-build >/dev/null

echo "==> Extracting build artifacts (dist/ and netlify.toml)…"
rm -rf dist netlify.toml
docker cp wasm-proj-build-cont:/app/dist ./dist
docker cp wasm-proj-build-cont:/app/netlify.toml ./netlify.toml

echo "==> Cleaning up temp container…"
docker rm wasm-proj-build-cont >/dev/null

# Sanity check
if [[ ! -f "dist/index.html" ]] || [[ ! -f "netlify.toml" ]]; then
  echo "Error: dist/ or netlify.toml missing after build."
  exit 1
fi

echo "==> Ensuring Netlify auth (you may be prompted once)…"
# If no token, this will open a browser once; otherwise it’s a no-op.
netlify status >/dev/null 2>&1 || netlify login

echo "==> Creating site if it doesn't exist…"
# Will fail if name is taken by someone else; that's expected.
# If it's yours and already exists, this will print an error which we ignore.
set +e
netlify sites:create --name "$SITE_NAME" >/dev/null 2>&1
CREATE_RC=$?
set -e
if [[ $CREATE_RC -eq 0 ]]; then
  echo "   Site created: $SITE_NAME"
else
  echo "   Site exists (or name taken). Proceeding to deploy…"
fi

echo "==> Deploying to production…"
# --site can take a site name/ID you own; if site doesn't belong to you this will fail.
netlify deploy \
  --dir="dist" \
  --prod \
  --site="$SITE_NAME"

echo "✅ Done! Deployed to Netlify site: $SITE_NAME"
```

Make it executable:

```bash
chmod +x deploy_netlify.sh
```

---

## How to use

From your project root:

```bash
# Build and deploy in one go:
./deploy_netlify.sh my-wasm-demo-site
```

If you prefer to do it step-by-step:

```bash
# Build the builder image and extract dist/ + netlify.toml
docker build -f Dockerfile.build -t wasm-proj-build .
docker rm -f wasm-proj-build-cont >/dev/null 2>&1 || true
docker create --name wasm-proj-build-cont wasm-proj-build
docker cp wasm-proj-build-cont:/app/dist ./dist
docker cp wasm-proj-build-cont:/app/netlify.toml ./netlify.toml
docker rm wasm-proj-build-cont

# Then deploy (assumes `netlify-cli` installed & logged in)
netlify sites:create --name my-wasm-demo-site   # first time only (ignore error if already yours)
netlify deploy --dir=dist --prod --site=my-wasm-demo-site
```

---

## What your final tree will look like (after the build step)

```text
wasm-c-demo/
├─ dist/
│  ├─ index.html
│  ├─ main.js              # imports from ./pkg/…
│  └─ pkg/
│     ├─ c_demo.js
│     ├─ c_demo.wasm
│     ├─ cpp_demo.js
│     └─ cpp_demo.wasm
└─ netlify.toml            # publish = "dist"
```

That’s it — your C/C++ → WebAssembly demo will be served by Netlify with the correct paths, headers, and a one-liner deploy flow.
