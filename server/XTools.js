const { promisify } = require('util')

/**
 * X11 colors
 */
const XColor = {
  Green: 0xadff2f << 32,
  Blue: 0x54c2cc << 32,
  Red: 0xde3700 << 32
}

// use xev to get keycodes
// TODO: move to config file
const XKey = {
  // change color
  r: 27,
  g: 42,
  b: 56,
  // exit
  ESC: 49,
  Space: 65,
  q: 24,
  // clearscreen
  c: 54,
  // screenshot
  s: 39,
  // drawing mode
  d: 40,
  // end drawing mode
  e: 26,
  // undo
  u: 30,
  // end drawing mode key code
  // CTRL
  CTRL: 37,
  // SHIFT
  SHIFT: 50
}

const XEvent = {
  KeyPress: 2,
  KeyRelease: 3,
  ButtonPress: 4,
  ButtonRelease: 5,
  MotionNotify: 6,
  EnterNotify: 7,
  LeaveNotify: 8,
  FocusIn: 9,
  FocusOut: 10,
  KeymapNotify: 11,
  Expose: 12,
  GraphicsExpose: 13,
  NoExpose: 14,
  VisibilityNotify: 15,
  CreateNotify: 16,
  DestroyNotify: 17,
  UnmapNotify: 18,
  MapNotify: 19,
  MapRequest: 20,
  ReparentNotify: 21,
  ConfigureNotify: 22,
  ConfigureRequest: 23,
  GravityNotify: 24,
  ResizeRequest: 25,
  CirculateNotify: 26,
  CirculateRequest: 27,
  PropertyNotify: 28,
  SelectionClear: 29,
  SelectionRequest: 30,
  SelectionNotify: 31,
  ColormapNotify: 32,
  ClientMessage: 33,
  MappingNotify: 34,
  GenericEvent: 35,
  LASTEvent: 36
}

/**
 * Draw a quadratic curve between the last two points
 */
const XQuadraticCurveTo = (X, display, wid, gc, points) => {
  // Number of line segments for approximation
  const segments = 15
  const l = points.length - 1
  let { x: currentX, y: currentY } = points[l]
  const { x: xc, y: yc } = points[l - 1]
  // Calculate control point
  const cpx = (currentX + xc) / 2
  const cpy = (currentY + yc) / 2

  // Calculate points along the curve using interpolation
  for (let i = 0; i <= segments; i++) {
    const t = i / segments
    const u = 1 - t
    const pointX = u * u * currentX + 2 * u * t * cpx + t * t * xc
    const pointY = u * u * currentY + 2 * u * t * cpy + t * t * yc

    // Draw a line segment between the current point and the calculated point
    X.PolyLine(display, wid, gc, [currentX, currentY, pointX, pointY])

    // Update the current point
    currentX = pointX
    currentY = pointY
  }
}

/**
 * Get window parents
 * I'm using i3wm - need locate the correct window container
 */
const getParents = async (X, root, src, parents) => {
  X.QueryTree = promisify(X.QueryTree, X)
  let tree = await X.QueryTree(src)
  parents.push(tree.parent)
  if (tree.parent == root) {
    return parents
  } else {
    return await getParents(X, root, tree.parent, parents)
  }
}

/**
 * Get under root geometry
 */
const getTopGeom = async (X, parents) => {
  let geom
  X.GetGeometry = promisify(X.GetGeometry, X)
  // remove root
  parents.splice(-1)
  // wid top window before root
  let wid = parents.pop()
  return await X.GetGeometry(wid)
}

/**
 * Get window name
 */
const getWindowName = async (X, wid) => {
  X.GetProperty = promisify(X.GetProperty, X)
  let prop = await X.GetProperty(0, wid, X.atoms.WM_NAME, 0, 0, 10000)
  //console.log("prop:", prop);
  if (prop.type == 398) return prop.data.toString('utf8')
  return ''
}

/**
 * Convert web color to X11 color
 */
const webColorToXColor = (color) => {
  let xcolor = typeof color === 'number' ? color : '0x' + color.substring(1)
  return Number(xcolor)
}

module.exports = {
  getParents,
  getTopGeom,
  getWindowName,
  webColorToXColor,
  XQuadraticCurveTo,
  XColor,
  XKey,
  XEvent
}
