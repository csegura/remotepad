const express = require('express')
const app = express()
const http = require('http')
const server = http.createServer(app)
const path = require('path')
const { Server } = require('socket.io')
const { XRemotePad } = require('./XRemotePad')

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

server.listen(3000, () => {
  console.log(`listening on ${server.address()}:3000`)
  console.log(' ESC/Space/Q - Exit')
  console.log(' r/g/b - Change color (in drawing mode)')
  console.log(' q - Quit server')
  //
  console.log('Running...')
  console.log('CTRL+SHIFT+d to activate/deactivate overlay mode')
  ;(async () => {
    await XRemotePad(wid, io)
  })()
})
