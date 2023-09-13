const express = require('express')
const app = express()
const http = require('http')
const server = http.createServer(app)
const path = require('path')
const { Server } = require('socket.io')
const { XRemotePad } = require('./XRemotePad')
const config = require('./config')

const stdin = process.openStdin()
stdin.setRawMode(true)
stdin.setEncoding('utf8')
stdin.resume()

const wid = parseInt(process.argv[2])

// socket.io server
const io = new Server(server)

// static client interface
app.use(express.static(path.resolve('./client')))

app.get('/', function (req, res) {
  res.render('index')
})

// stop server
stdin.on('data', async function (key) {
  console.log(key)
  switch (key) {
    case 'q':
    case 'Q':
    case 0x001b: // ESC
      process.exit()
  }
})

server.listen(config.port, () => {
  console.log(`Runnig on port ${config.port}`)
  console.log('ESC/Space/q - Exit')

  console.log(`CTRL+SHIFT control keys
      +d to activate drawing mode
      +r/g/b to change color
      +e to disable drawing mode`)
  //
  ;(async () => {
    await XRemotePad(wid, io)
  })()
})
