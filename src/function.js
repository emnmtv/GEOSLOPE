const { Schema, model } = require('mongoose');
const { connectToDatabase } = require('./database');

// Moisture readings now include deviceId and optional location + sensor extras
const MoistureReadingSchema = new Schema(
  {
    value: { type: Number, required: true },
    source: { type: String, default: 'arduino' },
    deviceId: { type: String, required: true, index: true },
    // Optional auxiliary sensor fields
    humidity: { type: Number },
    temperature: { type: Number },
    tilt: { type: Number },
    lat: { type: Number },
    lng: { type: Number }
  },
  { timestamps: true }
);

const MoistureReading = model('MoistureReading', MoistureReadingSchema);

// Device model to track the latest known location per device
const DeviceSchema = new Schema(
  {
    deviceId: { type: String, required: true, unique: true, index: true },
    lat: { type: Number },
    lng: { type: Number },
    name: { type: String },
    modelUrl: { type: String },
    modelName: { type: String }
  },
  { timestamps: true }
);

const Device = model('Device', DeviceSchema);

async function saveMoisture(value, source = 'arduino', deviceId = 'default-device', lat, lng, humidity, temperature, tilt) {
  await connectToDatabase();
  const reading = new MoistureReading({ value, source, deviceId, lat, lng, humidity, temperature, tilt });
  // If a reading includes location, update the device's last known location
  if (typeof lat === 'number' && typeof lng === 'number') {
    await Device.findOneAndUpdate(
      { deviceId },
      { $set: { lat, lng } },
      { upsert: true, new: true }
    );
  }
  return await reading.save();
}

async function getLatestMoisture(deviceId) {
  await connectToDatabase();
  const query = deviceId ? { deviceId } : {};
  return await MoistureReading.findOne(query).sort({ createdAt: -1 }).lean();
}

async function getMoistureReadings(limit = 50, deviceId) {
  await connectToDatabase();
  const safeLimit = Math.max(1, Math.min(500, Number(limit) || 50));
  const query = deviceId ? { deviceId } : {};
  return await MoistureReading.find(query).sort({ createdAt: -1 }).limit(safeLimit).lean();
}

async function setDeviceLocation(deviceId, lat, lng, name) {
  await connectToDatabase();
  return await Device.findOneAndUpdate(
    { deviceId },
    { $set: { lat, lng, ...(name ? { name } : {}) } },
    { upsert: true, new: true }
  ).lean();
}

async function getDeviceLocation(deviceId) {
  await connectToDatabase();
  const device = await Device.findOne({ deviceId }).lean();
  if (device) return device;
  // Fallback: try to infer from the latest reading with coordinates
  const latestWithLoc = await MoistureReading.findOne({ deviceId, lat: { $ne: null }, lng: { $ne: null } })
    .sort({ createdAt: -1 })
    .lean();
  if (latestWithLoc) return { deviceId, lat: latestWithLoc.lat, lng: latestWithLoc.lng, updatedAt: latestWithLoc.createdAt };
  return null;
}

async function setDeviceModel(deviceId, modelUrl, modelName) {
  await connectToDatabase();
  if (!modelUrl || typeof modelUrl !== 'string') throw new Error('modelUrl is required');
  const update = { modelUrl };
  if (modelName) update.modelName = modelName;
  const device = await Device.findOneAndUpdate(
    { deviceId },
    { $set: update },
    { upsert: true, new: true }
  ).lean();
  return { deviceId: device.deviceId, modelUrl: device.modelUrl, modelName: device.modelName };
}

async function getDeviceModel(deviceId) {
  await connectToDatabase();
  const device = await Device.findOne({ deviceId }).lean();
  if (!device || !device.modelUrl) return null;
  return { deviceId: device.deviceId, modelUrl: device.modelUrl, modelName: device.modelName };
}

module.exports = {
  saveMoisture,
  getLatestMoisture,
  getMoistureReadings,
  setDeviceLocation,
  getDeviceLocation,
  setDeviceModel,
  getDeviceModel
};


