// https://github.com/<owner>/<repo>/releases/download/<tag>/<filename>
var repo = "ublitzjs/uwsjs-fork"
var tag = "v0.0.4"

module.exports = async function downloadBinary(paramTag = tag, paramFilename = 'uws_' + process.platform + '_' + process.arch + '_' + process.versions.modules + '.node') {

  var archiveName = paramFilename + ".tar.gz";
  var link = "https://github.com/" + repo + "/releases/download/" + paramTag + "/" + archiveName;

  console.log("Expected binary - " + paramFilename);
  require("node:child_process").execSync(`curl -sSL \"${link}\" -o ${archiveName} && tar -xzf ${archiveName}`, {stdio: "inherit"}); 
  require("node:fs").rmSync(archiveName)
  console.log("finished")
}

if (process.argv[2] == "default") {
  module.exports();
}
