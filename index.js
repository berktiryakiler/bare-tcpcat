const Buffer = require('bare-buffer')
const native = require('./lib/native')
const { TcpCatError } = require('./lib/errors')

function checkPort(port) {
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new TcpCatError('EINVAL', `port out of range: ${port}`)
  }
}

function checkHost(host) {
  if (typeof host !== 'string' || host.length === 0) {
    throw new TcpCatError('EINVAL', 'host must be a non-empty string')
  }
}

async function tcpCat(host, port, data) {
  checkHost(host)
  checkPort(port)

  if (data == null) {
    throw new TcpCatError('EINVAL', 'request body is required')
  }

  const body = await native.tcpCat(host, port, data)
  return Buffer.from(body)
}

module.exports = tcpCat
module.exports.tcpCat = tcpCat
module.exports.TcpCatError = TcpCatError
