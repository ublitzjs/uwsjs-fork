import {createRequire} from "node:module"
var require = createRequire(import.meta.url)
var uws = require("./uws.cjs")

export default uws;

export const {
  // App
  App,
  SSLApp,
  H3App,

  // Listen options
  LIBUS_LISTEN_EXCLUSIVE_PORT,

  // µSockets functions
  us_listen_socket_close,
  us_socket_local_port,
  
  // Compression options
  DISABLED,
  SHARED_COMPRESSOR,
  SHARED_DECOMPRESSOR,
  DEDICATED_DECOMPRESSOR,
  DEDICATED_COMPRESSOR,
  DEDICATED_COMPRESSOR_3KB,
  DEDICATED_COMPRESSOR_4KB,
  DEDICATED_COMPRESSOR_8KB,
  DEDICATED_COMPRESSOR_16KB,
  DEDICATED_COMPRESSOR_32KB,
  DEDICATED_COMPRESSOR_64KB,
  DEDICATED_COMPRESSOR_128KB,
  DEDICATED_COMPRESSOR_256KB,
  DEDICATED_DECOMPRESSOR_32KB,
  DEDICATED_DECOMPRESSOR_16KB,
  DEDICATED_DECOMPRESSOR_8KB,
  DEDICATED_DECOMPRESSOR_4KB,
  DEDICATED_DECOMPRESSOR_2KB,
  DEDICATED_DECOMPRESSOR_1KB,
  DEDICATED_DECOMPRESSOR_512B,

  // Temporary KV store
  getString,
  setString,
  getInteger,
  setInteger,
  incInteger,
  lock,
  unlock,
  getIntegerKeys,
  getStringKeys,
  deleteString,
  deleteInteger,
  deleteStringCollection,
  deleteIntegerCollection,
  _cfg,
  getParts,
} = uws;
