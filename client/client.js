const canvas = document.getElementById('drawing')
const background = document.getElementById('background')
const bgCtx = background.getContext('2d')
const drawCtx = canvas.getContext('2d')
const container = document.getElementById('container')
const loader = document.getElementById('loader')
const statusChip = document.getElementById('status-chip')
const colorPicker = document.getElementById('btn-color')
const splashInfo = document.getElementById('splash-info')

let ws = null
let connected = false
let capturing = false
let drawingEnabled = false
let penOnly = true
let currentColor = '#adff2f'
let brushSize = 2
let sourceImage = null
let captureMeta = null
let pendingCaptureMeta = null
let resizeCaptureTimer = null
let lastClearTapAt = 0
let firstStrokeFeedbackPending = false
let statusResetTimer = null
let captureRequestedAtMs = 0
let lastAppliedCaptureSeq = 0
let reconnectDelay = 500
let serverInstanceId = null
const RECONNECT_MIN = 500
const RECONNECT_MAX = 5000

const viewport = {
  offsetX: 0,
  offsetY: 0,
  width: 0,
  height: 0,
  scale: 1
}

let drawing = false
let currentStrokeSource = []
const strokesSource = []

const presetMap = {
  fine: 2,
  medium: 4,
  bold: 8
}

const colors = [
  { element: 'btn-color-r', color: '#de3700' },
  { element: 'btn-color-g', color: '#adff2f' },
  { element: 'btn-color-b', color: '#54c2cc' }
]

const setStatus = (message, state = 'connecting') => {
  statusChip.textContent = message
  statusChip.title = message
  statusChip.classList.remove('ready', 'connecting', 'capturing', 'error', 'confirm')
  statusChip.classList.add(state)
}

const flashStatus = (message, state = 'error', ms = 1000) => {
  if (statusResetTimer) {
    clearTimeout(statusResetTimer)
  }
  setStatus(message, state)
  statusResetTimer = setTimeout(() => {
    if (connected) {
      if (capturing) {
        setStatus('capturing', 'capturing')
      } else if (drawingEnabled) {
        setStatus('ready', 'ready')
      } else {
        setStatus('connected', 'connecting')
      }
    } else {
      setStatus('...', 'connecting')
    }
  }, ms)
}

const showLoader = (show) => {
  loader.hidden = !show
}

const showBackground = (show) => {
  background.hidden = !show
}

const wsSend = (event, data = {}) => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ event, data }))
  }
}

const ensureCaptureMeta = () => {
  if (!sourceImage) {
    return null
  }
  if (!captureMeta) {
    captureMeta = {
      sourceWidth: sourceImage.width,
      sourceHeight: sourceImage.height,
      scaleX: 1,
      scaleY: 1,
      captureSeq: 0,
      captureTs: 0
    }
  }
  captureMeta.scaleX = captureMeta.sourceWidth / sourceImage.width
  captureMeta.scaleY = captureMeta.sourceHeight / sourceImage.height
  return captureMeta
}

const paintBackground = () => {
  const dpr = window.devicePixelRatio || 1
  bgCtx.clearRect(0, 0, background.width / dpr, background.height / dpr)
  if (!sourceImage) {
    return
  }
  bgCtx.imageSmoothingEnabled = true
  bgCtx.imageSmoothingQuality = 'high'
  bgCtx.drawImage(
    sourceImage,
    viewport.offsetX,
    viewport.offsetY,
    viewport.width,
    viewport.height
  )
}

const drawStrokeSegmentCanvas = (points) => {
  if (points.length < 2) {
    return
  }

  drawCtx.strokeStyle = points[points.length - 1].color
  drawCtx.lineWidth = points[points.length - 1].lineWidth
  drawCtx.lineCap = 'round'
  drawCtx.lineJoin = 'round'
  drawCtx.beginPath()
  drawCtx.moveTo(points[0].x, points[0].y)

  for (let i = 1; i < points.length - 1; i += 1) {
    const midX = (points[i].x + points[i + 1].x) / 2
    const midY = (points[i].y + points[i + 1].y) / 2
    drawCtx.quadraticCurveTo(points[i].x, points[i].y, midX, midY)
  }

  const last = points[points.length - 1]
  drawCtx.lineTo(last.x, last.y)
  drawCtx.stroke()
}

const sourcePointToCanvasPoint = (point) => {
  const meta = ensureCaptureMeta()
  const scaleX = meta?.scaleX || 1
  const encodedX = point.x / scaleX
  const encodedY = point.y / (meta?.scaleY || 1)
  const encodedW = point.lineWidth / scaleX
  return {
    x: viewport.offsetX + encodedX * viewport.scale,
    y: viewport.offsetY + encodedY * viewport.scale,
    lineWidth: encodedW * viewport.scale,
    color: point.color
  }
}

const strokeSourceToCanvas = (stroke) => stroke.map(sourcePointToCanvasPoint)

const redrawLocal = () => {
  const dpr = window.devicePixelRatio || 1
  drawCtx.clearRect(0, 0, canvas.width / dpr, canvas.height / dpr)
  strokesSource.forEach((stroke) => {
    drawStrokeSegmentCanvas(strokeSourceToCanvas(stroke))
  })
  if (currentStrokeSource.length > 1) {
    drawStrokeSegmentCanvas(strokeSourceToCanvas(currentStrokeSource))
  }
}

const setActiveColorButton = (value) => {
  colors.forEach((entry) => {
    const el = document.getElementById(entry.element)
    el.classList.toggle('is-active', entry.color.toLowerCase() === value.toLowerCase())
  })
}

const setColor = (value) => {
  currentColor = value
  colorPicker.value = value
  setActiveColorButton(value)
}

const setActivePreset = (presetId) => {
  ;['preset-fine', 'preset-medium', 'preset-bold'].forEach((id) => {
    document.getElementById(id).classList.toggle('is-active', id === presetId)
  })
}

const setBrushSize = (value, presetId = null) => {
  brushSize = Number(value)
  if (presetId) {
    setActivePreset(presetId)
  } else {
    setActivePreset('')
  }
}

const getCanvasPoint = (event) => {
  const rect = canvas.getBoundingClientRect()
  return {
    x: event.clientX - rect.left,
    y: event.clientY - rect.top,
    lineWidth: brushSize,
    color: currentColor
  }
}

const computeViewport = () => {
  const dpr = window.devicePixelRatio || 1
  const cssW = canvas.width / dpr
  const cssH = canvas.height / dpr

  if (!sourceImage || sourceImage.width <= 0 || sourceImage.height <= 0) {
    viewport.offsetX = 0
    viewport.offsetY = 0
    viewport.width = cssW
    viewport.height = cssH
    viewport.scale = 1
    return
  }

  const sx = cssW / sourceImage.width
  const sy = cssH / sourceImage.height
  const scale = Math.min(sx, sy)
  const width = Math.max(1, Math.round(sourceImage.width * scale))
  const height = Math.max(1, Math.round(sourceImage.height * scale))
  viewport.offsetX = Math.floor((cssW - width) / 2)
  viewport.offsetY = Math.floor((cssH - height) / 2)
  viewport.width = width
  viewport.height = height
  viewport.scale = scale
}

const isPointInViewport = (point) => (
  point.x >= viewport.offsetX &&
  point.x <= viewport.offsetX + viewport.width &&
  point.y >= viewport.offsetY &&
  point.y <= viewport.offsetY + viewport.height
)

const clampPointToViewport = (point) => ({
  x: Math.min(Math.max(point.x, viewport.offsetX), viewport.offsetX + viewport.width),
  y: Math.min(Math.max(point.y, viewport.offsetY), viewport.offsetY + viewport.height),
  lineWidth: point.lineWidth,
  color: point.color
})

const canvasPointToSourcePoint = (point) => {
  const meta = ensureCaptureMeta()
  const encodedX = (point.x - viewport.offsetX) / viewport.scale
  const encodedY = (point.y - viewport.offsetY) / viewport.scale
  const encodedW = point.lineWidth / viewport.scale
  return {
    x: encodedX * (meta?.scaleX || 1),
    y: encodedY * (meta?.scaleY || 1),
    lineWidth: encodedW * (meta?.scaleX || 1),
    color: point.color
  }
}

const clearLocalStrokes = () => {
  strokesSource.length = 0
  currentStrokeSource = []
  redrawLocal()
}

const requestCapture = (reason = 'manual') => {
  if (!connected || capturing) {
    return
  }
  if (drawing) {
    return
  }
  capturing = true
  drawingEnabled = false
  captureRequestedAtMs = performance.now()
  showLoader(true)
  setStatus('capturing', 'capturing')
  wsSend('capture', { reason })
}

const scheduleResizeRecapture = () => {
  if (!connected || capturing || !sourceImage) {
    return
  }
  if (resizeCaptureTimer) {
    clearTimeout(resizeCaptureTimer)
  }
  resizeCaptureTimer = setTimeout(() => {
    requestCapture('resize')
  }, 250)
}

const resizeCanvases = () => {
  const toolbar = document.querySelector('.toolbar')
  const availableHeight = window.innerHeight - toolbar.offsetHeight
  const cssWidth = container.clientWidth
  const cssHeight = availableHeight
  const dpr = window.devicePixelRatio || 1

  // Set canvas backing store to device pixels for sharp rendering
  canvas.width = Math.round(cssWidth * dpr)
  canvas.height = Math.round(cssHeight * dpr)
  background.width = Math.round(cssWidth * dpr)
  background.height = Math.round(cssHeight * dpr)

  // CSS size stays at layout pixels
  canvas.style.width = cssWidth + 'px'
  canvas.style.height = cssHeight + 'px'
  background.style.width = cssWidth + 'px'
  background.style.height = cssHeight + 'px'

  // Scale drawing contexts so coordinates stay in CSS pixels
  bgCtx.setTransform(dpr, 0, 0, dpr, 0, 0)
  drawCtx.setTransform(dpr, 0, 0, dpr, 0, 0)

  if (sourceImage) {
    computeViewport()
    paintBackground()
    redrawLocal()
    scheduleResizeRecapture()
  }
}

const beginStroke = (event) => {
  if (!connected || !drawingEnabled || capturing) {
    return
  }
  if (penOnly && event.pointerType !== 'pen') {
    return
  }

  event.preventDefault()
  const point = getCanvasPoint(event)
  if (!isPointInViewport(point)) {
    flashStatus('outside', 'error', 900)
    return
  }

  if (firstStrokeFeedbackPending && navigator.vibrate) {
    navigator.vibrate(10)
    firstStrokeFeedbackPending = false
  }

  canvas.setPointerCapture?.(event.pointerId)
  drawing = true
  const sourcePoint = canvasPointToSourcePoint(point)
  currentStrokeSource = [sourcePoint]
  wsSend('drawstart', sourcePoint)
  redrawLocal()
}

const moveStroke = (event) => {
  if (!drawing) {
    return
  }

  event.preventDefault()
  const point = clampPointToViewport(getCanvasPoint(event))
  const sourcePoint = canvasPointToSourcePoint(point)
  currentStrokeSource.push(sourcePoint)
  redrawLocal()
  wsSend('drawmove', sourcePoint)
}

const endStroke = (event) => {
  if (!drawing) {
    return
  }

  event.preventDefault()
  drawing = false
  if (currentStrokeSource.length > 0) {
    strokesSource.push([...currentStrokeSource])
    currentStrokeSource = []
    redrawLocal()
    wsSend('drawend')
  }
}

const clearCanvas = () => {
  const now = Date.now()
  if (now - lastClearTapAt > 1200) {
    lastClearTapAt = now
    flashStatus('tap again', 'confirm', 1200)
    return
  }
  lastClearTapAt = 0
  // Cancel any in-progress stroke
  drawing = false
  clearLocalStrokes()
  wsSend('drawclear')
}

const undoStroke = () => {
  // Cancel any in-progress stroke first
  if (drawing) {
    drawing = false
    currentStrokeSource = []
    redrawLocal()
    wsSend('drawend')
    return
  }
  if (strokesSource.length === 0) {
    return
  }
  strokesSource.pop()
  redrawLocal()
  wsSend('undo')
}

const endSession = () => {
  if (!window.confirm('End session and disable overlay?')) {
    return
  }
  drawing = false
  drawingEnabled = false
  clearLocalStrokes()
  showBackground(false)
  showLoader(true)
  splashInfo.hidden = false
  setStatus('ended', 'connecting')
  wsSend('end')
}

const applyImage = async (blob, frameMeta) => {
  const nextImage = await createImageBitmap(blob)
  sourceImage = nextImage

  if (frameMeta && frameMeta.captureSeq > 0) {
    if (frameMeta.captureSeq <= lastAppliedCaptureSeq) {
      capturing = false
      drawingEnabled = true
      return
    }
    lastAppliedCaptureSeq = frameMeta.captureSeq
  }

  if (frameMeta) {
    captureMeta = {
      sourceWidth: frameMeta.sourceWidth,
      sourceHeight: frameMeta.sourceHeight,
      scaleX: 1,
      scaleY: 1,
      captureSeq: frameMeta.captureSeq,
      captureTs: frameMeta.captureTs
    }
  } else if (!captureMeta) {
    captureMeta = {
      sourceWidth: sourceImage.width,
      sourceHeight: sourceImage.height,
      encodedWidth: sourceImage.width,
      encodedHeight: sourceImage.height,
      scaleX: 1,
      scaleY: 1,
      captureSeq: 0,
      captureTs: 0
    }
  }
  ensureCaptureMeta()

  computeViewport()
  paintBackground()
  showBackground(true)
  showLoader(false)
  splashInfo.hidden = true
  drawingEnabled = true
  capturing = false
  firstStrokeFeedbackPending = true

  const readyW = captureMeta?.sourceWidth || sourceImage.width
  const readyH = captureMeta?.sourceHeight || sourceImage.height
  setStatus(`ready ${readyW}x${readyH}`, 'ready')
  redrawLocal()
}

const handleControlMessage = (msg) => {
  if (msg.event === 'hello') {
    const newId = msg.data?.instanceId || null
    if (serverInstanceId !== null && newId !== serverInstanceId) {
      clearLocalStrokes()
      lastAppliedCaptureSeq = 0
    }
    serverInstanceId = newId
    requestCapture('connect')
    return
  }
  if (msg.event === 'clear') {
    clearLocalStrokes()
    redrawLocal()
    return
  }
  if (msg.event === 'capture_started') {
    capturing = true
    drawingEnabled = false
    showLoader(true)
    setStatus('capturing', 'capturing')
  } else if (msg.event === 'capture_ready') {
    const sourceWidth = Number(msg.data?.sourceWidth || 0)
    const sourceHeight = Number(msg.data?.sourceHeight || 0)
    const captureSeq = Number(msg.data?.capture_seq || 0)
    const captureTs = Number(msg.data?.capture_ts || 0)

    if (sourceWidth > 0 && sourceHeight > 0) {
      pendingCaptureMeta = {
        sourceWidth,
        sourceHeight,
        captureSeq,
        captureTs
      }
    } else {
      pendingCaptureMeta = null
    }
    setStatus('receiving', 'capturing')
  } else if (msg.event === 'capture_failed') {
    capturing = false
    drawingEnabled = Boolean(sourceImage)
    showLoader(false)
    setStatus(msg.data?.message || 'failed', 'error')
  } else if (msg.event === 'error') {
    flashStatus(msg.data?.message || 'server error', 'error', 1500)
  }
}

const connectWebSocket = () => {
  const url = `ws://${location.host}`
  console.log('WS connecting to', url)
  ws = new WebSocket(url)
  ws.binaryType = 'blob'

  ws.onopen = () => {
    console.log('WS open')
    connected = true
    reconnectDelay = RECONNECT_MIN
    container.classList.remove('disconnected')
    splashInfo.hidden = true
    showLoader(false)
    setStatus('connected', 'connecting')
  }

  ws.onclose = (e) => {
    console.log('WS close', e.code, e.reason)
    connected = false
    capturing = false
    drawing = false
    drawingEnabled = false
    pendingCaptureMeta = null
    container.classList.add('disconnected')
    splashInfo.hidden = false
    showBackground(Boolean(sourceImage))
    showLoader(true)
    setStatus('...', 'connecting')
    setTimeout(connectWebSocket, reconnectDelay)
    reconnectDelay = Math.min(reconnectDelay * 1.5, RECONNECT_MAX)
  }

  ws.onerror = (e) => {
    console.warn('WebSocket error', e)
  }

  ws.onmessage = async (event) => {
    if (typeof event.data === 'string') {
      try {
        handleControlMessage(JSON.parse(event.data))
      } catch (_) {
        flashStatus('unexpected message', 'error', 1200)
      }
      return
    }

    const frameMeta = pendingCaptureMeta
    pendingCaptureMeta = null
    await applyImage(event.data, frameMeta)
  }
}

const bindToolbar = () => {
  document.getElementById('btn-load').addEventListener('click', () => requestCapture('capture'))
  document.getElementById('btn-clear').addEventListener('click', clearCanvas)
  const btnPenOnly = document.getElementById('btn-pen-only')
  btnPenOnly.addEventListener('click', () => {
    penOnly = !penOnly
    btnPenOnly.classList.toggle('is-active', penOnly)
  })
  document.getElementById('btn-undo').addEventListener('click', undoStroke)
  document.getElementById('btn-screenshot').addEventListener('click', () => {
    wsSend('screenshot')
    flashStatus('saved', 'ready', 1200)
  })
  document.getElementById('btn-fullscreen').addEventListener('click', () => {
    const el = document.documentElement
    if (document.fullscreenElement || document.webkitFullscreenElement) {
      (document.exitFullscreen || document.webkitExitFullscreen).call(document)
    } else {
      (el.requestFullscreen || el.webkitRequestFullscreen)?.call(el)
    }
  })
  if (!document.documentElement.requestFullscreen && !document.documentElement.webkitRequestFullscreen) {
    document.getElementById('btn-fullscreen').hidden = true
  }
  document.getElementById('btn-end').addEventListener('click', endSession)

  document.getElementById('preset-fine').addEventListener('click', () => {
    setBrushSize(presetMap.fine, 'preset-fine')
  })
  document.getElementById('preset-medium').addEventListener('click', () => {
    setBrushSize(presetMap.medium, 'preset-medium')
  })
  document.getElementById('preset-bold').addEventListener('click', () => {
    setBrushSize(presetMap.bold, 'preset-bold')
  })

  colors.forEach((entry) => {
    document.getElementById(entry.element).addEventListener('click', () => {
      setColor(entry.color)
    })
  })

  colorPicker.addEventListener('input', (event) => {
    setColor(event.target.value)
  })
}

const bindCanvas = () => {
  canvas.addEventListener('pointerdown', beginStroke)
  canvas.addEventListener('pointermove', moveStroke)
  canvas.addEventListener('pointerup', endStroke)
  canvas.addEventListener('pointercancel', endStroke)
}

window.addEventListener('resize', resizeCanvases)
window.addEventListener('orientationchange', () => {
  setTimeout(resizeCanvases, 100)
})
document.addEventListener('fullscreenchange', resizeCanvases)
document.addEventListener('webkitfullscreenchange', resizeCanvases)

window.addEventListener('load', () => {
  bindToolbar()
  bindCanvas()
  setColor(currentColor)
  setBrushSize(brushSize, 'preset-fine')
  resizeCanvases()
  showBackground(false)
  showLoader(true)
  setStatus('...', 'connecting')
  connectWebSocket()
})
