const path = require('path')
const assert = require('assert')
const dotenv = require('dotenv')

// read in the .env file
dotenv.config({ path: path.resolve(__dirname, '../.env') })

const { SERVER_PORT } = process.env

// validate the required configuration information
assert(SERVER_PORT, 'SERVER_PORT configuration is required.')

module.exports = {
  port: SERVER_PORT
}
