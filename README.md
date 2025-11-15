wasmmhmm wasm build system, minimal and fast
---
- WIP fast, small WASM system for web, OS, and other platforms. 

- get the code/files:
```sh
git clone https://github.com/mrhappynice/wasmmhmm.git && cd wasmmhmm
```

working build:

- set x on scripts:
```sh
chmod +x build-build.sh get-files.sh prod-build.sh run-prod.sh
```

- build the builder docker:
```sh
./build-build.sh
```

- get the pkg files:
```sh
./get-files.sh
```

- build the production docker:
```sh
./run-prod.sh
```

- browse to http://localhost:8080
