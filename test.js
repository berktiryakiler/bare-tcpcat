const test = require('brittle')
const Buffer = require('bare-buffer')
const tcpCat = require('.')
const { TcpCatError } = require('./lib/errors')

// 1.1.1.1 literal IP, no DNS lookup in the test path.
// Connection: close so the read ends, keep-alive would not send EOF
const HTTP_GET =
  'GET / HTTP/1.1\r\nHost: cloudflare.com\r\nConnection: close\r\n\r\n'

// End-to-end: connect, write, read until EOF, get a Buffer back.
test('tcpCat returns HTTP response from Cloudflare', async (t) => {
  const response = await tcpCat('1.1.1.1', 80, HTTP_GET)

  t.ok(Buffer.isBuffer(response))
  t.ok(response.length > 0)
  t.is(response.toString('utf8', 0, 4), 'HTTP')
})

// Request body can be a Buffer, not only a string.
test('tcpCat accepts Buffer request body', async (t) => {
  const response = await tcpCat('1.1.1.1', 80, Buffer.from(HTTP_GET))

  t.ok(Buffer.isBuffer(response))
  t.ok(response.length > 0)
})

// Port range is checked in JS before the addon runs.
test('tcpCat rejects bad port before touching native code', async (t) => {
  try {
    await tcpCat('1.1.1.1', 70000, HTTP_GET)
    t.fail('should have thrown')
  } catch (err) {
    t.is(err.code, 'EINVAL')
    t.ok(err instanceof TcpCatError)
  }
})
