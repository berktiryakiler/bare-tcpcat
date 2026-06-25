class TcpCatError extends Error {
  constructor(code, message) {
    super(message)
    this.name = 'TcpCatError'
    this.code = code
  }
}

module.exports = { TcpCatError }
