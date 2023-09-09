const x11 = require('x11')
const { promisify } = require('util')
const path = require('path')
const XTools = require('./XTools')
const { XGetScreen, XTakeScreenshot } = require('./XGetScreen')

const debug = false
x11.createClient = promisify(x11.createClient, x11)

let points = []
let drawHistory = []
let currentColor = XTools.XColor.Green

const lineWidthFactor = 1.5

const log = (...msg) => {
  if (debug) console.log(...msg)
}

const XRemotePad = async (target, io) => {
  const display = await x11.createClient()
  const root = display.screen[0].root
  const X = display.client

  X.on('error', (err) => {
    console.error('X error: ', err)
  })

  X.require = promisify(X.require, X)

  // get information about target window
  let parents = await XTools.getParents(X, root, target, [target])
  let geom = await XTools.getTopGeom(X, parents)

  // get target window name
  let target_name = await XTools.getWindowName(X, target)
  console.info('Overlay over: %s (%d)', target_name, target)

  const Composite = await X.require('composite')
  const Render = await X.require('render')

  Composite.GetOverlayWindow = promisify(Composite.GetOverlayWindow, Composite)

  // The only way to access the XID of this window is via the
  // CompositeGetOverlayWindow request.
  // Initially, the Composite Overlay Window is unmapped.
  // (https://linux.die.net/man/3/xcompositegetoverlaywindow)
  let overlay = await Composite.GetOverlayWindow(target)

  // console.log('Overlay: ', overlay)

  // Unmap overlay (in-visible)
  X.UnmapWindow(overlay)

  Composite.RedirectSubwindows(root, Composite.Redirect.Automatic)

  X.ChangeWindowAttributes(overlay, {
    eventMask:
      x11.eventMask.KeyPress |
      x11.eventMask.KeyRelease |
      x11.eventMask.KeymapState |
      x11.eventMask.ResizeRedirect |
      //x11.eventMask.Exposure |
      x11.eventMask.ButtonPress |
      x11.eventMask.ButtonRelease |
      x11.eventMask.PointerMotion
  })

  // put overlay over target
  X.MoveResizeWindow(overlay, geom.xPos, geom.yPos, geom.width, geom.height)
  // name the overlay window
  X.ChangeProperty(0, overlay, X.atoms.WM_NAME, X.atoms.STRING, 8, 'REMOTEPAD')

  // GrabKeys using Object.keys
  // Control keys require CTRL+SHIFT+KEY
  const ShiftMask = 1 << 0
  const ControlMask = 1 << 2

  Object.keys(XTools.XKey).forEach((key) => {
    X.GrabKey(root, 0, ShiftMask | ControlMask, XTools.XKey[key], 0, 1)
  })

  // drawing context
  let gc = X.AllocID()
  X.CreateGC(gc, overlay, {
    foreground: XTools.XColor.Green,
    lineWidth: 3,
    capStyle: 2, // 0:NotLast, 1:Butt, 2:Round, 3:Projecting
    fillStyle: 0 // 0:Solid, 1:Tiled, 2:Stippled, 3:OpaqueStippled
  })

  let mapped = false
  let drawing = false

  // handle events
  X.on('event', async function (ev) {
    // EV.KeyPress
    if (ev.type == XTools.XEvent.KeyPress) {
      // take screenshot
      if (ev.keycode == XTools.XKey.s) {
        console.info('Taking shoot of window:', overlay)
        XTakeScreenshot('./screenshots', target, target_name.trim())
      }

      // map overlay / begin drawing
      else if (ev.keycode == XTools.XKey.d) {
        activateOverlay()
      }
      // unmap overlay / end
      else if (ev.keycode == XTools.XKey.e) {
        deactivateOverlay()
      }

      // change colors (r,g,b)
      else if (ev.keycode == XTools.XKey.r) {
        changeColor(XTools.XColor.Red)
      } else if (ev.keycode == XTools.XKey.g) {
        changeColor(XTools.XColor.Green)
      } else if (ev.keycode == XTools.XKey.b) {
        changeColor(XTools.XColor.Blue)
      }

      // clear screen
      else if (ev.keycode == XTools.XKey.c) {
        clearDraw(false)
      }

      // undo
      else if (ev.keycode == XTools.XKey.u) {
        clearDraw(true)
      }

      // space / esc / q
      else if (
        ev.keycode == XTools.XKey.ESC ||
        ev.keycode == XTools.XKey.Space ||
        ev.keycode == XTools.XKey.q
      ) {
        terminate()
        return
      }
    }

    // Mouse Drawing

    // Left - begin drawing
    else if (ev.type == XTools.XEvent.ButtonPress && ev.keycode == 1) {
      drawStart({ x: ev.x, y: ev.y, lineWidth: 3, color: currentColor })
    }

    // Left - stop
    else if (ev.type == XTools.XEvent.ButtonRelease && ev.keycode == 1) {
      drawEnd()
    }

    // Right - clear
    else if (ev.type == XTools.XEvent.ButtonPress && ev.keycode == 3) {
      deactivateOverlay()
    }

    // Motion drawing
    else if (ev.type == XTools.XEvent.MotionNotify && drawing) {
      drawMove({ x: ev.x, y: ev.y, lineWidth: 3, color: currentColor })
    }
  })

  X.on('end', function () {
    log('client destroyed')
  })

  const changeColor = (color) => {
    X.ChangeGC(gc, { foreground: color })
    currentColor = color
  }

  const activateOverlay = () => {
    if (!mapped) {
      log('Overlay Activated: ', overlay, target_name)
      X.MapWindow(overlay)
      mapped = true
    }
  }

  const deactivateOverlay = () => {
    if (mapped) {
      log('Overlay Deactivated: ', overlay, target_name)
      X.UnmapWindow(overlay)
      mapped = false
    }
  }

  const terminate = () => {
    drawHistory = []
    points = []
    X.close()
    X.terminate()
    io.close()
  }

  const draw = (points) => {
    const l = points.length - 1
    if (l >= 3) {
      X.ChangeGC(gc, {
        lineWidth: points[l - 1].lineWidth * lineWidthFactor,
        foreground: XTools.webColorToXColor(points[l - 1].color)
      })
      XTools.XQuadraticCurveTo(X, display, overlay, gc, points)
    }
  }

  const drawStart = (point) => {
    points = []
    points.push(point)
    drawing = true
  }

  const drawEnd = () => {
    drawHistory.push([...points])
    points = []
    drawing = false
  }

  const drawMove = (point) => {
    if (!drawing) return
    points.push(point)
    draw(points)
  }

  const redraw = () => {
    log('redraw ' + drawHistory.length)
    if (drawHistory.length > 0) {
      drawHistory.pop()
      drawHistory.map((hist) => {
        points = []
        hist.map((point) => {
          points.push(point)
          draw(points)
        })
      })
      points = []
    }
  }

  const clearDraw = (rdraw) => {
    // Not working I tried almost everything :-(
    // X.ClearArea(overlay, 0, 0, 0, 0, 0)

    deactivateOverlay()
    setTimeout(() => {
      activateOverlay()
      log('overlay activated')
      if (rdraw) redraw()
    }, 50)
  }

  const undo = () => {
    clearDraw(true)
  }

  // manage comms
  XRemote(io, {
    target,
    activateOverlay,
    deactivateOverlay,
    drawStart,
    drawMove,
    drawEnd,
    clearDraw,
    undo
  })
}

const XRemote = (io, pad) => {
  const eventMap = [
    { remote: 'drawstart', local: pad.drawStart },
    { remote: 'drawmove', local: pad.drawMove },
    { remote: 'drawend', local: pad.drawEnd },
    { remote: 'drawclear', local: pad.clearDraw },
    { remote: 'undo', local: pad.undo },
    { remote: 'end', local: pad.deactivateOverlay },
    { remote: 'log', local: log }
  ]

  io.on('connection', (socket) => {
    log('Connected')

    socket.on('get_screen', async (msg) => {
      log('rcvd get_screen: ' + msg)
      socket.emit('get_screen', 'done')
    })

    socket.on('capture', async () => {
      log('rcvd: capture')
      let coords = await XGetScreen(path.resolve('./client'), pad.target)
      log('Calculated coords:', coords)
      socket.emit('get_screen', 'done')
      log('Capture done')
      pad.activateOverlay()
    })

    eventMap.forEach((ev) => {
      socket.on(ev.remote, async (data) => {
        log('rcvd: ' + ev.remote + ' - ', data)
        ev.local(data)
      })
    })
  })
}

module.exports = {
  XRemotePad
}
