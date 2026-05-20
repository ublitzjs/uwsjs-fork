### Forked from https://github.com/uNetworking/uWebSockets.js
## Modifications
1) Parallelised compilation + caching (9 minutes -> 1:30 minutes CI)
2) CI can run tests for all platforms
3) branches "binaries" and "binaries-asan" have had their Git history reset
4) GitHub Releases are published from "dist" branch, which contains only an installer. Consumer installs 2.5MB binary with "postinstall" script in package.json; 
5) ESM wrapper exports DeclarativeResponse + modern syntax
6) TypeScript has adjustable HTTP header autocompletion, "TemplatedApp" methods return "this".
7) Each "...Wrapper.h" exports a namespace
8) Each template function now uses enum "TCP|SSL|QUIC|CACHE" and guards with static_assert - type safety

### Installation
* `npm install "github:ublitzjs/uwsjs-fork#v0.0.1"`
* `bun install "github:ublitzjs/uwsjs-fork#v0.0.1" && bun pm trust uwsjs-fork` or use "trustedDependencies" in package.json
* Import with `import {App} from "uwsjs-fork"` or `require("uwsjs-fork")`
<!-- * Browse the [documentation](https://unetworking.github.io/uWebSockets.js/generated/functions/App.html) and see the [main repo](https://github.com/uNetworking/uWebSockets). There are tons of [examples](examples) but here's the gist of it all: -->

### :handshake: Permissively licensed
Intellectual property, all rights reserved.

Where such explicit notice is given, source code is licensed Apache License 2.0 which is a permissive OSI-approved license with very few limitations. This "fork" is of nothing but licensed source code, made available under another product name.
