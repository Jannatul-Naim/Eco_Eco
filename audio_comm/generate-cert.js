const fs = require('fs');
const path = require('path');

const keyPath = path.join(__dirname, 'key.pem');
const certPath = path.join(__dirname, 'cert.pem');

if (fs.existsSync(keyPath) && fs.existsSync(certPath)) {
  console.log('Certificates already exist.');
  process.exit(0);
}

const selfsigned = require('selfsigned');

(async () => {
  const pems = await selfsigned.generate(
    [{ name: 'commonName', value: 'rover-audio' }],
    { days: 365, keySize: 2048, algorithm: 'sha256' }
  );
  fs.writeFileSync(keyPath, pems.private);
  fs.writeFileSync(certPath, pems.cert);
  console.log('Generated key.pem and cert.pem');
})();
