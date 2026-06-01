// https://github.com/<owner>/<repo>/releases/download/<tag>/<filename>
var repo = "ublitzjs/uwsjs-fork"
var tag = "v0.0.2"

module.exports = async function downloadBinary(paramTag = tag, paramFilename = 'uws_' + process.platform + '_' + process.arch + '_' + process.versions.modules + '.node') {

  var archiveName = paramFilename + ".tar.gz";
  var link = "https://github.com/" + repo + "/releases/download/" + paramTag + "/" + archiveName;

  var execSync = require("node:child_process").execSync;
  try {
    console.info("Fetching " + archiveName + " for uwsjs-fork");
    execSync(`curl -sSL \"${link}\" -o ${archiveName}`);
  } catch (e) {
    console.error("uwsjs-fork does not provide a binary \"" + paramFilename + "\" for your NodeJS version", e)
  }
  try {
    console.info("Unpacking" + paramFilename + " for uwsjs-fork");
    execSync(`tar -xzf ${archiveName}`, {stdio: "inherit"}); 
    require("node:fs").rmSync(archiveName)
  } catch (e) {
    console.error("Cannot unpack the archive", e)
  }
}

if (process.argv[2] == "default") {
  module.exports();
}
