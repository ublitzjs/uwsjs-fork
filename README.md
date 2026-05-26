> Forked from https://github.com/uNetworking/uWebSockets.js. I hope that some of the adjustments might be considered to be used within original uWebSockets.js someday.
## Modifications
1) Parallelised compilation + caching (9 minutes -> 1:30 minutes CI)
2) CI can run tests for all platforms
3) branches "binaries" and "binaries-asan" have had their Git history reset
4) examples/AsyncFunction.js alongside HTTP displays WebSockets case, where an aborted client can trigger the crash of a server.
5) GitHub Releases are published from "dist" branch, which contains only an installer. Consumer installs one GZIP binary (684KB-2.1MB) with "postinstall" script in package.json, compared to 30-40MB before
6) ESM wrapper exports DeclarativeResponse + modern syntax
7) TypeScript has adjustable HTTP header autocompletion, "TemplatedApp" methods return "this" for possible "TemplatedApp" extending.
8) Each "...Wrapper.h" exports a namespace
9) Each template function now uses enum "TCP|SSL|QUIC|CACHE" and guards with static_assert - centralised type-safe logic
10) Somehow smaller executables (happened by chance)

Before on Linux x64 or arm64
|Project|uWebSockets.js|uwsjs-fork|
|---|---|---|
|GZIP|2.5MB|684KB|
|Normal|7MB|2.4MB|

Before on Darwin x64 or arm64 (MacOS)
|Project|uWebSockets.js|uwsjs-fork|
|---|---|---|
|GZIP|1.8MB|1.7MB|
|Normal|5.4MB|5.2MB|

Before on Windows x64
|Project|uWebSockets.js|uwsjs-fork|
|---|---|---|
|GZIP|2.2MB|2.1MB|
|Normal|6.5MB|5.6MB|

### Installation
* `npm install "github:ublitzjs/uwsjs-fork#v0.0.1"`
* `bun install "github:ublitzjs/uwsjs-fork#v0.0.1" && bun pm trust uwsjs-fork` or use "trustedDependencies" in package.json
* Import with `import {App} from "uwsjs-fork"` or `require("uwsjs-fork")`
<!-- * Browse the [documentation](https://unetworking.github.io/uWebSockets.js/generated/functions/App.html) and see the [main repo](https://github.com/uNetworking/uWebSockets). There are tons of [examples](examples) but here's the gist of it all: -->

### :handshake: Permissively licensed
Intellectual property, all rights reserved.

Where such explicit notice is given, source code is licensed Apache License 2.0 which is a permissive OSI-approved license with very few limitations. This "fork" is of nothing but licensed source code, made available under another product name.
