const x11 = require('x11')
const { promisify } = require('util')
const Jimp = require('jimp')

const GET_IMAGE_MASK = 0xffffffff

x11.createClient = promisify(x11.createClient, x11)

/**
 * Create a Jimp image from a X11 image
 */
const CreateImage = (img, width, height) => {
  // use jimp to transform image
  const shoot = new Jimp(width, height)

  if (img) {
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const r = img.data[(x + width * y) * 4 + 2]
        const g = img.data[(x + width * y) * 4 + 1]
        const b = img.data[(x + width * y) * 4 + 0]
        const a = 1

        shoot.setPixelColor(Jimp.rgbaToInt(r, g, b, a), x, y)
      }
    }
  }

  return shoot
}

/**
 * Create a buffer from a Jimp image
 */
const CreateImageBuffer = async (img) => {
  return await img.getBufferAsync(Jimp.MIME_JPEG)
}

/**
 * Get an image of a window
 */
const XGetScreen = async (wid) => {
  console.log('Taking shoot of window:', wid)

  const display = await x11.createClient()
  const root = display.screen[0].root
  const X = display.client
  let img = null
  let geom = null

  try {
    X.on('error', (err) => {
      console.error('getScreen - x11 client error: ', err)
    })

    // X.on('end', function () {
    //   console.log('getScreen client destroyed')
    // })

    X.GetGeometry = promisify(X.GetGeometry, X)
    X.GetImage = promisify(X.GetImage, X)
    X.TranslateCoordinates = promisify(X.TranslateCoordinates, X)

    geom = await X.GetGeometry(wid)
    //console.log("win %d geom:", wid, geom);

    const coords = await X.TranslateCoordinates(wid, root, 0, 0)
    //console.log("win %d coords:", wid, coords);

    //const rgeom = await X.GetGeometry(root)
    //console.log("root %d geom:", root, rgeom);

    img = await X.GetImage(
      2,
      root,
      coords.destX,
      coords.destY,
      geom.width,
      geom.height,
      GET_IMAGE_MASK
    )
  } catch (err) {
    console.error('X Error', err)
  }

  X.close()
  X.terminate()

  return { img, geom }
}

/**
 * Get an image of a window as Buffer
 */
const XGetBufferedScreen = async (wid) => {
  const { img, geom } = await XGetScreen(wid)
  const shoot = CreateImage(img, geom.width, geom.height)
  return await CreateImageBuffer(shoot)
}

/**
 * Take a screenshot of our window
 * Save it as screen.jpg under client path
 * Return coords translated from root
 */
const XScreen2Jpg = async (path, wid, fileName = 'screen') => {
  const { img, geom } = await XGetScreen(wid)

  if (img) {
    const shoot = CreateImage(img, geom.width, geom.height)
    shoot.write = promisify(shoot.write, shoot)
    shoot.write(`${path}/${fileName}.jpg`)
  }
}

const XTakeScreenshot = async (path, wid, name = 'no_name') => {
  // date in format YYMMDD-HHMM take in to account timezones
  const dateTime = new Date().toISOString().replace(/[-:]/g, '').slice(2, 13)
  // filename is overlay + timestamp
  const fileName = `screen_${name.trim()}_${dateTime}`
  await XScreen2Jpg(path, wid, fileName)
}

module.exports = {
  XGetScreen,
  XScreen2Jpg,
  XTakeScreenshot,
  XGetBufferedScreen
}
