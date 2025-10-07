const mongoose = require('mongoose');
const fs = require('fs');
const path = require('path');

let cachedConnection = null;

function readMongoUriFromHello() {
  const helloPath = path.resolve(__dirname, '..', 'hello.txt');
  try {
    const raw = fs.readFileSync(helloPath, 'utf8').trim();
    return raw;
  } catch (err) {
    throw new Error('Failed to read MongoDB URI from hello.txt: ' + err.message);
  }
}

async function connectToDatabase() {
  if (cachedConnection) return cachedConnection;

  const mongoUri = process.env.MONGODB_URI || readMongoUriFromHello();

  mongoose.set('strictQuery', true);

  await mongoose.connect(mongoUri, {
    serverSelectionTimeoutMS: 10000
  });

  cachedConnection = mongoose.connection;

  cachedConnection.on('error', (err) => {
    console.error('MongoDB connection error:', err);
  });

  return cachedConnection;
}

module.exports = {
  connectToDatabase
};


