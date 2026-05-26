/* Non-SSL is simply App() */
const uWS = require('uwsjs-fork');
var maxSizeOutOfTheBlue = 1024*100; //1000KiB  
	var server = uWS.App();
	server.get('/', new uWS.DeclarativeResponse().writeHeader('content-type', 'text/plain').end('Hi'))
	.get('/id/:id', new uWS.DeclarativeResponse().writeHeader('content-type', 'text/plain')
					.writeHeader('x-powered-by', 'benchmark')
					.writeParameterValue("id")
					.write(" ")
					.writeQueryValue("name")
					.end())
  server.get("/here", (res)=>{console.info("REQ"); res.end("hi")})
	server.post('/json', (res, req) => {
    res.onAborted(()=>{ res.aborted = true; })
    res.collectBody(maxSizeOutOfTheBlue, (maybeArrayBuffer)=>{
      // res.aborted != true
      if(maybeArrayBuffer != null) {
        console.log(JSON.parse(Buffer.from(maybeArrayBuffer).toString()))
        res.end("OK!!!");
      } else {
        res.end("NOT OK!!!");
      }
    })
	})
	.listen(3000, (listenSocket) => {
		if (listenSocket) {
			console.log('Listening to port 3000')
		}
	})

