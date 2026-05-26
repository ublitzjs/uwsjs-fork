/* SSL/non-SSL example with async/await functions */

async function someAsyncTask() {
  return delay(500, 'Hey wait for me!');
}

const uWS = require('uwsjs-fork');
const delay = require("node:timers/promises").setTimeout
const port = 9001;

uWS./*SSL*/App({
  key_file_name: 'misc/key.pem',
  cert_file_name: 'misc/cert.pem',
  passphrase: '1234'
}).get('/*', async (res) => {
  /* Can't return or yield from here without responding or attaching an abort handler */
  res.onAborted(() => {
    res.aborted = true;
  });

  /* Awaiting will yield and effectively return to C++, so you need to have called onAborted */
  let r = await someAsyncTask();

  /* If we were aborted, you cannot respond */
  if (!res.aborted) {
    res.cork(() => {
      res.end(r);
    });
  }
}).ws({
  open(ws) {
    console.log("WebSocket client connected");
  },
  async message(ws) {
    let r = await someAsyncTask();
    /* If we were aborted, you cannot respond */
    if (ws.aborted) return;
    let isBinary = false;
    ws.send(r, isBinary);
  },
  close(ws) {
    /* "message" handler might still be performing an asynchronous task */
    ws.aborted = true;
  }
}).listen(port, (token) => {
  if (token) {
    console.log('Listening to port ' + port);
  } else {
    console.log('Failed to listen to port ' + port);
  }
});
