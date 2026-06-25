# bare-addon-example: tcpCat

This example shows a Bare native addon that opens a TCP connection, writes a request, reads until the peer closes, and returns the response as a buffer.

Reference: 
[mafintosh/6ab3fc85511ca2bacb12187f9b34cef3](https://gist.github.com/mafintosh/6ab3fc85511ca2bacb12187f9b34cef3)
Using:
[holepunchto/bare-addon](https://github.com/holepunchto/bare-addon).

## Usage

```js
const tcpCat = require('.')

const response = await tcpCat(
  '1.1.1.1',
  80,
  'GET / HTTP/1.1\r\nHost: cloudflare.com\r\nConnection: close\r\n\r\n'
)

console.log(response.toString('utf8', 0, 20))
```

`index.js` validates arguments and wraps the result in buffer `binding.c` runs the libuv connect/write/read loop.

## Build and Test

```bash
npm install
npx bare-make generate
npx bare-make build
npx bare-make install
npm test
```

Tests need outbound network (HTTP to Cloudflare on port 80). Expect `3/3` pass...

**Windows:** if `clang-cl` is missing, install [LLVM](https://releases.llvm.org/), add `C:\Program Files\LLVM\bin` to `PATH`, then:

```bash
npx bare-make generate --no-cache
npx bare-make build
npx bare-make install
```

After editing `binding.c`:

```bash
npx bare-make build && npx bare-make install
```

## My Thoughts

I have worked on OpenHarmony(Huawei) native bridges (NAPI/ANI) and before that, async TCP with Boost Beast for Ticketmaster’s REST API and core networking & system.

Bare is a version of that stack: libjs for the bridge and libuv for async I/O, instead of NAPI/ANI and the inner API / system I/O layer underneath.

**Bare**

```
JavaScript (policy, Buffer)
       |
libjs (native addon bridge)
       |
libuv (event loop, TCP)
       |
  OS / network
```

**OpenHarmony** 

```
ArkTS(Extended TypeScript) / ETS (kits)
       |
NAPI / ANI (native bridge)
       |
inner API + services 
       |
      OS
```

