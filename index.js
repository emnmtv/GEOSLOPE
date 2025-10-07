const express = require('express');
const cors = require('cors');
const bodyParser = require('body-parser');
const path = require('path');
const fs = require('fs');
const multer = require('multer');
const { connectToDatabase } = require('./src/database');
const { saveMoisture, getLatestMoisture, getMoistureReadings, setDeviceLocation, getDeviceLocation, setDeviceModel, getDeviceModel } = require('./src/function');

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(bodyParser.json());
// Simple API call logger
app.use((req, res, next) => {
  const start = Date.now();
  const { method, url } = req;
  const q = Object.keys(req.query || {}).length ? ` query=${JSON.stringify(req.query)}` : '';
  if (url.startsWith('/api/')) {
    console.log(`[API] → ${method} ${url}${q}`);
    if (method !== 'GET' && req.is('application/json')) {
      // Avoid logging large binary bodies like uploads
      const bodyPreview = url.startsWith('/api/uploads') ? '{file: <binary>}' : JSON.stringify(req.body);
      console.log(`[API]    body=${bodyPreview}`);
    }
  }
  res.on('finish', () => {
    if (url.startsWith('/api/')) {
      console.log(`[API] ← ${method} ${url} ${res.statusCode} ${Date.now() - start}ms`);
    }
  });
  next();
});

// Ensure uploads directory exists and serve it statically
const uploadsDir = path.join(__dirname, 'uploads');
if (!fs.existsSync(uploadsDir)) {
  fs.mkdirSync(uploadsDir, { recursive: true });
}
app.use('/uploads', express.static(uploadsDir));

// Multer setup for model uploads (GLB/GLTF)
const storage = multer.diskStorage({
  destination: function (req, file, cb) {
    cb(null, uploadsDir);
  },
  filename: function (req, file, cb) {
    const safeBase = file.originalname.replace(/[^a-zA-Z0-9_.-]/g, '_');
    const stamp = Date.now();
    cb(null, `${stamp}_${safeBase}`);
  }
});
const upload = multer({
  storage,
  limits: { fileSize: 50 * 1024 * 1024 },
  fileFilter: function (req, file, cb) {
    const ok = /\.(glb|gltf)$/i.test(file.originalname);
    if (!ok) return cb(new Error('Only .glb or .gltf files are allowed'));
    cb(null, true);
  }
});

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'index.html'));
});

app.post('/api/moisture', async (req, res) => {
  try {
    const { value, source, deviceId = 'default-device', lat, lng, humidity, temperature, tilt } = req.body || {};
    if (typeof value !== 'number') {
      return res.status(400).json({ error: 'value (number) is required' });
    }
    const hasLat = typeof lat === 'number';
    const hasLng = typeof lng === 'number';
    const saved = await saveMoisture(
      value,
      source,
      deviceId,
      hasLat ? lat : undefined,
      hasLng ? lng : undefined,
      typeof humidity === 'number' ? humidity : undefined,
      typeof temperature === 'number' ? temperature : undefined,
      typeof tilt === 'number' ? tilt : undefined
    );
    console.log('[API] saved reading', {
      id: saved && saved._id ? String(saved._id) : undefined,
      deviceId,
      value,
      humidity,
      temperature,
      tilt,
      createdAt: saved && saved.createdAt ? saved.createdAt : undefined
    });
    res.status(201).json(saved);
  } catch (err) {
    console.error('[API] /api/moisture error:', err && err.message ? err.message : err);
    res.status(500).json({ error: 'Failed to save moisture reading' });
  }
});

app.get('/api/moisture/latest', async (req, res) => {
  try {
    const { deviceId } = req.query;
    const doc = await getLatestMoisture(deviceId);
    console.log('[API] latest', { deviceId, value: doc && doc.value, createdAt: doc && doc.createdAt });
    res.json(doc || {});
  } catch (err) {
    console.error('[API] /api/moisture/latest error:', err && err.message ? err.message : err);
    res.status(500).json({ error: 'Failed to get latest reading' });
  }
});

app.get('/api/moisture', async (req, res) => {
  try {
    const { limit, deviceId } = req.query;
    const docs = await getMoistureReadings(limit, deviceId);
    console.log('[API] history', { deviceId, limit, count: Array.isArray(docs) ? docs.length : 0 });
    res.json(docs);
  } catch (err) {
    console.error('[API] /api/moisture error:', err && err.message ? err.message : err);
    res.status(500).json({ error: 'Failed to get readings' });
  }
});

// Device location endpoints
app.get('/api/device/:deviceId/location', async (req, res) => {
  try {
    const { deviceId } = req.params;
    const loc = await getDeviceLocation(deviceId);
    if (!loc) return res.status(404).json({ error: 'Location not found' });
    res.json(loc);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Failed to get device location' });
  }
});

app.post('/api/device/:deviceId/location', async (req, res) => {
  try {
    const { deviceId } = req.params;
    const { lat, lng, name } = req.body || {};
    if (typeof lat !== 'number' || typeof lng !== 'number') {
      return res.status(400).json({ error: 'lat and lng (numbers) are required' });
    }
    const saved = await setDeviceLocation(deviceId, lat, lng, name);
    res.status(200).json(saved);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Failed to set device location' });
  }
});

// Device 3D model endpoints
app.get('/api/device/:deviceId/model', async (req, res) => {
  try {
    const { deviceId } = req.params;
    const model = await getDeviceModel(deviceId);
    if (!model) return res.status(404).json({ error: 'Model not set' });
    res.json(model);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Failed to get device model' });
  }
});

app.post('/api/device/:deviceId/model', async (req, res) => {
  try {
    const { deviceId } = req.params;
    const { modelUrl, modelName } = req.body || {};
    if (!modelUrl || typeof modelUrl !== 'string') {
      return res.status(400).json({ error: 'modelUrl (string) is required' });
    }
    const saved = await setDeviceModel(deviceId, modelUrl, modelName);
    res.status(200).json(saved);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Failed to set device model' });
  }
});

// Upload endpoints
app.get('/api/uploads', async (req, res) => {
  try {
    const files = fs.readdirSync(uploadsDir)
      .filter(f => /\.(glb|gltf)$/i.test(f))
      .map(name => {
        const m = name.match(/^(\d+?)_(.+)$/);
        const displayName = m ? m[2] : name;
        return { name, url: `/uploads/${name}`, displayName };
      });
    res.json(files);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Failed to list uploads' });
  }
});

app.post('/api/uploads', upload.single('file'), (req, res) => {
  try {
    if (!req.file) return res.status(400).json({ error: 'file is required' });
    const url = `/uploads/${req.file.filename}`;
    res.status(201).json({ name: req.file.filename, url, displayName: req.file.originalname });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Failed to upload file' });
  }
});

connectToDatabase()
  .then(() => {
    app.listen(PORT, () => {
      console.log('Server listening on port ' + PORT);
    });
  })
  .catch((err) => {
    console.error('Database connection failed:', err);
    process.exit(1);
  });


