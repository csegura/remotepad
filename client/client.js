const canvas = document.getElementById('drawing')
const background = document.getElementById('background')
const backgroundCtx = background.getContext('2d')
const loader = document.getElementById('loader')
const container = document.getElementById('container')

const socket = io()
const drawAll = new Drawall(canvas, { guides: false })

let connected = false

// by default we want the image fit on canvas
let fit = true
// image ratio - calculated on load
let ratio = null
// last loaded image (screen capture)
let img

// log - if activated logs are send to server
const log = false

// fast colors
const colors = [
  {
    element: 'btn-color-r',
    color: '#de3700'
  },
  {
    element: 'btn-color-g',
    color: '#adff2f'
  },
  {
    element: 'btn-color-b',
    color: '#54c2cc'
  }
]

/**
 * draw image on canvas
 */
const drawImage = (ctx, image, fit) => {
  if (fit) {
    ctx.drawImage(
      image,
      // source rectangle
      0,
      0,
      image.width,
      image.height,
      // destination rectangle
      0,
      0,
      ctx.canvas.width,
      ctx.canvas.height
    )
  } else {
    ctx.drawImage(image, 0, 0)
  }
}

/**
 * resize canvas to fit image
 * if fit is true, the canvas will be resized to fit the image
 */
const resizeCanvas = (canvas, image, container, fit) => {
  canvas.width = fit ? container.clientWidth : image.width
  canvas.height = fit ? container.clientHeight : image.height
}

/**
 * Clear canvas
 * Maintain last image
 */
const clearCanvas = (ev) => {
  ev.preventDefault()
  drawAll.clear()
  socket.emit('drawclear')
}

const undoDraw = (_) => {
  drawAll.undo()
  socket.emit('undo')
}

/**
 * Request connection to server
 * server should take a screenshot
 * and send it back with a get_screen event
 */
const loadScreen = () => {
  if (connected) socket.emit('capture')
}

/**
 * Send end event to server
 * Close connection
 */
const sendEnd = () => {
  drawAll.clear()
  showBackground(false)
  showLoader(true)
  socket.emit('end')
  connected = false
}

/**
 * load image from url
 */
const loadImage = (src) => {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.onload = () => resolve(img)
    img.onerror = reject
    img.src = src
  })
}

/**
 * load image from buffer
 */
const loadImageFromBuffer = (buf) => {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.onload = () => resolve(img)
    img.onerror = reject
    img.src = 'data:image/jpeg;base64,' + buf
  })
}

/**
 * check if image fit on canvas
 */
const isNecesaryFit = (canvas, image) => {
  return image.width > canvas.width && image.height > canvas.height
}

/**
 * calculate ratio to use
 */
const calcRatio = (fit, src, dst) => {
  ratio = { x: 1, y: 1 }
  if (fit) {
    ratio = {
      x: src.width / dst.width,
      y: src.height / dst.height
    }
  }
  emitLog(
    // Image size
    `Img ${src.width} ${src.height} ` +
      // canvas size
      `Canvas ${dst.width} ${dst.height}`
  )
  emitLog(`Ratio ${ratio.x} ${ratio.y}`)
}

/**
 * Adjust position using ratio
 * we sent a copy of the position
 * in a new object
 */
const adjustPosition = (pos) => {
  return {
    x: pos.x * ratio.x,
    y: pos.y * ratio.y,
    lineWidth: pos.w * 10,
    color: pos.c
  }
}

/**
 * lock/unlock scroll
 */
const lockUnlockScroll = (e) => {
  if (container.style.overflow === 'hidden') {
    container.style.overflow = 'scroll'
    e.target.style.background = '#e7be4d'
  } else {
    container.style.overflow = 'hidden'
    e.target.style.background = '#adff2f'
  }
}

/**
 * server & client log
 */
const emitLog = (msg, obj) => {
  if (!log) return
  socket.emit('log', msg)
  if (typeof obj === 'object') {
    socket.emit('log', JSON.stringify(obj))
  }
}

const showLoader = (show) => {
  loader.style.display = show ? 'block' : 'none'
  loader.style.background = connected ? '#000' : '#fff'
}

const showBackground = (show) => {
  background.style.display = show ? 'block' : 'none'
}

const attachUIEvents = () => {
  // events
  const clearButton = document.getElementById('btn-clear')
  const loadButton = document.getElementById('btn-load')
  const endButton = document.getElementById('btn-end')
  const scrollButton = document.getElementById('btn-scroll')
  const undoButton = document.getElementById('btn-undo')

  // fast color buttons
  colors.forEach((color) => {
    const el = document.getElementById(color.element)
    el.addEventListener('click', () => {
      drawAll.changeColor(color.color)
    })
  })

  // custom color
  const customColor = document.getElementById('btn-color')
  customColor.addEventListener('change', (ev) => {
    drawAll.changeColor(ev.target.value)
  })

  // rest of actions
  clearButton.addEventListener('click', clearCanvas)
  loadButton.addEventListener('click', loadScreen)
  scrollButton.addEventListener('click', lockUnlockScroll)
  undoButton.addEventListener('click', undoDraw)
  endButton.addEventListener('click', sendEnd)
}

const initDrawAll = () => {
  const menuHeight = 22
  // set initial canvas size
  canvas.width = window.innerWidth
  canvas.height = window.innerHeight - menuHeight

  // image loaded on background
  background.width = window.innerWidth
  background.height = window.innerHeight - menuHeight

  drawAll.clear()
  drawAll.changeColor(colors[1].color)

  drawAll.addEventListener('drawstart', (ev) => {
    socket.emit('drawstart', adjustPosition(ev.detail))
  })

  drawAll.addEventListener('drawmove', (ev) => {
    emitLog('drawmove', ev.detail)
    socket.emit('drawmove', adjustPosition(ev.detail))
  })

  drawAll.addEventListener('drawend', (ev) => {
    emitLog('drawend', ev.detail)
    socket.emit('drawend')
  })
}

const initComs = () => {
  socket.on('connect', () => {
    emitLog('Client Connected')
    drawAll.clear()
    showLoader(true)
    //socket.emit('capture')
    connected = true
  })

  socket.on('disconnect', (reason) => {
    connected = false
    showBackground(false)
    showLoader(true)
    if (reason === 'io server disconnect') {
      socket.connect()
    }
  })

  socket.on('screen', async (data) => {
    emitLog('rcvd screen')
    let img = await loadImageFromBuffer(data.buffer)
    emitLog('rcvd screen: ' + data.buffer.length)
    let fit = isNecesaryFit(canvas, img)
    // resize canvas, fit if necessary
    resizeCanvas(canvas, img, container, fit)
    drawImage(backgroundCtx, img, fit)
    calcRatio(fit, img, canvas)
    emitLog('ratio', ratio)
    showBackground(true)
    showLoader(false)
    connected = true
    drawAll.clear()
  })

  // /**
  //  * If there is an image available
  //  * draw it on canvas
  //  */
  // socket.on('get_screen', async function (msg) {
  //   img = await loadImage('/screen.jpg?' + Date.now())
  //   // hide loader
  //   showLoader(false)
  //   // check if image is too big
  //   fit = isNecesaryFit(canvas, img)
  //   emitLog('need fit: ', fit)
  //   // resize canvas, fit if necessary
  //   resizeCanvas(canvas, img, container, fit)
  //   drawImage(backgroundCtx, img, fit)

  //   calcRatio(fit, img, canvas)
  //   emitLog('ratio', ratio)

  //   emitLog('rcvd get_screen: ' + msg)
  //   showBackground(true)
  //   connected = true
  //   drawAll.clear()
  // })
}

window.onload = () => {
  attachUIEvents()
  initDrawAll()
  initComs()
}
