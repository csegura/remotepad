const x11 = require('x11')
const { promisify } = require('util')
const Jimp = require('jimp')

const GET_IMAGE_MASK = 0xffffffff

x11.createClient = promisify(x11.createClient, x11)

/**
 * Take a screenshot of our window
 * Save it as screen.jpg under client path
 * Return coords translated from root
 */
const XGetScreen = async (path, wid, fileName = 'screen') => {
  console.log('Taking shoot of window:', wid)

  const display = await x11.createClient()
  const root = display.screen[0].root
  const X = display.client

  X.on('error', (err) => {
    console.error('getScreen - x11 client error: ', err)
  })

  X.on('end', function () {
    console.log('getScreen client destroyed')
  })

  X.GetGeometry = promisify(X.GetGeometry, X)
  X.GetImage = promisify(X.GetImage, X)
  X.TranslateCoordinates = promisify(X.TranslateCoordinates, X)

  const geom = await X.GetGeometry(wid)
  //console.log("win %d geom:", wid, geom);

  const coords = await X.TranslateCoordinates(wid, root, 0, 0)
  //console.log("win %d coords:", wid, coords);

  try {
    const rgeom = await X.GetGeometry(root)
    //console.log("root %d geom:", root, rgeom);

    const img = await X.GetImage(
      2,
      root,
      coords.destX,
      coords.destY,
      geom.width,
      geom.height,
      GET_IMAGE_MASK
    )

    // use jimp to save image to jpg
    const shoot = new Jimp(geom.width, geom.height)

    if (img) {
      for (let y = 0; y < geom.height; y++) {
        for (let x = 0; x < geom.width; x++) {
          const r = img.data[(x + geom.width * y) * 4 + 2]
          const g = img.data[(x + geom.width * y) * 4 + 1]
          const b = img.data[(x + geom.width * y) * 4 + 0]
          const a = 1

          shoot.setPixelColor(Jimp.rgbaToInt(r, g, b, a), x, y)
        }
      }
    }

    shoot.write = promisify(shoot.write, shoot)
    await shoot.write(`${path}/${fileName}.jpg`)
  } catch (err) {
    console.error('X Error', err)
  }

  console.log('Saved screen to', path)
  X.close()
  X.terminate()

  return coords
}

const XTakeScreenshot = async (path, wid, name = 'no_name') => {
  // date in format YYMMDD-HHMM take in to account timezones
  const dateTime = new Date().toISOString().replace(/[-:]/g, '').slice(2, 13)
  // filename is overlay + timestamp
  const fileName = `screen_${name.trim()}_${dateTime}`
  await XGetScreen(path, wid, fileName)
}

module.exports = { XGetScreen, XTakeScreenshot }
