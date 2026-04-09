var jpeg = require('jpeg-js');

var CHUNK_SIZE = 4096;
var transferInProgress = false;

function isColorPlatform() {
  try {
    var info = Pebble.getActiveWatchInfo();
    var platform = info.platform || '';
    return (platform === 'basalt' || platform === 'chalk' || platform === 'emery' || platform === 'gabbro');
  } catch (e) {
    return true; // default to color
  }
}

function sendError(msg) {
  console.log('[playback] ERROR: ' + msg);
  try {
    Pebble.sendAppMessage({ 'ErrorMsg': msg }, null, null);
  } catch (e) { /* best effort */ }
}

function arrayBufferToBytes(ab) {
  var arr = new Uint8Array(ab);
  if (arr.length > 0) return arr;

  // Fallback for Pebble emulator
  var dv = new DataView(ab);
  arr = new Uint8Array(ab.byteLength);
  for (var i = 0; i < ab.byteLength; i++) {
    arr[i] = dv.getUint8(i);
  }
  return arr;
}

// Resize source RGBA pixels and quantize to GColor8 (8-bit, 64 colors)
function resizeAndQuantize(srcPixels, srcW, srcH, dstW, dstH) {
  var dst = new Uint8Array(dstW * dstH);

  var scale = Math.max(dstW / srcW, dstH / srcH);
  var scaledW = srcW * scale;
  var scaledH = srcH * scale;
  var offsetX = (dstW - scaledW) / 2;
  var offsetY = (dstH - scaledH) / 2;

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var srcX = Math.floor((x - offsetX) / scale);
      var srcY = Math.floor((y - offsetY) / scale);

      var r = 0, g = 0, b = 0;
      if (srcX >= 0 && srcX < srcW && srcY >= 0 && srcY < srcH) {
        var srcIdx = (srcY * srcW + srcX) * 4;
        r = srcPixels[srcIdx];
        g = srcPixels[srcIdx + 1];
        b = srcPixels[srcIdx + 2];
      }

      dst[y * dstW + x] = 0xC0 | ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
    }
  }
  return dst;
}

// Resize source RGBA pixels and dither to 1-bit B&W (GBitmapFormat1Bit)
// Uses Floyd-Steinberg dithering for good visual quality
// 1-bit format: each row is padded to 4-byte boundary, MSB first, 1=white 0=black
function resizeAndDither(srcPixels, srcW, srcH, dstW, dstH) {
  // First resize to grayscale buffer
  var gray = new Float32Array(dstW * dstH);

  var scale = Math.max(dstW / srcW, dstH / srcH);
  var scaledW = srcW * scale;
  var scaledH = srcH * scale;
  var offsetX = (dstW - scaledW) / 2;
  var offsetY = (dstH - scaledH) / 2;

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var srcX = Math.floor((x - offsetX) / scale);
      var srcY = Math.floor((y - offsetY) / scale);

      var lum = 0;
      if (srcX >= 0 && srcX < srcW && srcY >= 0 && srcY < srcH) {
        var srcIdx = (srcY * srcW + srcX) * 4;
        // Luminance: 0.299R + 0.587G + 0.114B
        lum = 0.299 * srcPixels[srcIdx] + 0.587 * srcPixels[srcIdx + 1] + 0.114 * srcPixels[srcIdx + 2];
      }
      gray[y * dstW + x] = lum;
    }
  }

  // Floyd-Steinberg dithering
  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var idx = y * dstW + x;
      var old = gray[idx];
      var nw = old < 128 ? 0 : 255;
      gray[idx] = nw;
      var err = old - nw;

      if (x + 1 < dstW) gray[idx + 1] += err * 7 / 16;
      if (y + 1 < dstH) {
        if (x > 0) gray[(y + 1) * dstW + (x - 1)] += err * 3 / 16;
        gray[(y + 1) * dstW + x] += err * 5 / 16;
        if (x + 1 < dstW) gray[(y + 1) * dstW + (x + 1)] += err * 1 / 16;
      }
    }
  }

  // Pack into 1-bit format with rows padded to 4-byte boundary
  var rowBytes = Math.ceil(dstW / 32) * 4;
  var dst = new Uint8Array(rowBytes * dstH);

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      if (gray[y * dstW + x] <= 128) {
        // 1 = black in Pebble 1-bit format
        var byteIdx = y * rowBytes + Math.floor(x / 8);
        dst[byteIdx] |= (1 << (x % 8));
      }
    }
  }

  return dst;
}

function processJpeg(jpegData) {
  console.log('[playback] Decoding JPEG (' + jpegData.length + ' bytes)');
  try {
    var raw = jpeg.decode(jpegData, { useTArray: true });
    console.log('[playback] Decoded: ' + raw.width + 'x' + raw.height);

    var W = 144, H = 106;
    var isRound = false;

    try {
      var info = Pebble.getActiveWatchInfo();
      var platform = info.platform || '';
      if (platform === 'emery') {
        W = 200; H = 228 - 62;
      } else if (platform === 'gabbro') {
        W = 260; H = 260 / 2; // 50/50 split for round
        isRound = true;
      } else if (platform === 'chalk') {
        W = 180; H = 180 / 2; // 50/50 split for round
        isRound = true;
      }
    } catch (e) {}

    var imageData;
    if (isRound) {
      // Use Aspect Fit (Math.min) for round screens to avoid cropping into the curve
      imageData = resizeAndQuantizeFit(raw.data, raw.width, raw.height, W, H);
    } else if (isColorPlatform()) {
      imageData = resizeAndQuantize(raw.data, raw.width, raw.height, W, H);
    } else {
      imageData = resizeAndDither(raw.data, raw.width, raw.height, W, H);
    }

    console.log('[playback] Processed (' + W + 'x' + H + '): ' + imageData.length + ' bytes');
    sendImageToWatch(imageData, W, H);
  } catch (ex) {
    sendError('Decode: ' + ex.message);
  }
}

// Aspect Fit version for round screens
function resizeAndQuantizeFit(srcPixels, srcW, srcH, dstW, dstH) {
  var dst = new Uint8Array(dstW * dstH);
  for (var i = 0; i < dst.length; i++) dst[i] = 0xC0; // Initialize with black

  var scale = Math.min(dstW / srcW, dstH / srcH);
  var scaledW = srcW * scale;
  var scaledH = srcH * scale;
  var offsetX = (dstW - scaledW) / 2;
  var offsetY = (dstH - scaledH) / 2;

  for (var y = 0; y < dstH; y++) {
    for (var x = 0; x < dstW; x++) {
      var srcX = Math.floor((x - offsetX) / scale);
      var srcY = Math.floor((y - offsetY) / scale);

      if (srcX >= 0 && srcX < srcW && srcY >= 0 && srcY < srcH) {
        var srcIdx = (srcY * srcW + srcX) * 4;
        var r = srcPixels[srcIdx];
        var g = srcPixels[srcIdx + 1];
        var b = srcPixels[srcIdx + 2];
        dst[y * dstW + x] = 0xC0 | ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
      }
    }
  }
  return dst;
}

function downloadAndSend(url) {
  if (transferInProgress) {
    console.log('[playback] Transfer in progress, skipping');
    return;
  }

  console.log('[playback] Downloading image');
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.responseType = 'arraybuffer';
  xhr.onload = function() {
    if (xhr.status !== 200) {
      sendError('Download ' + xhr.status);
      return;
    }
    var ab = xhr.response;
    console.log('[playback] Downloaded ' + ab.byteLength + ' bytes');
    var bytes = arrayBufferToBytes(ab);
    if (bytes.length === 0) {
      sendError('Empty download');
      return;
    }
    processJpeg(bytes);
  };
  xhr.onerror = function() {
    sendError('Download failed');
  };
  xhr.send();
}

function sendImageToWatch(data, width, height) {
  transferInProgress = true;
  var totalChunks = Math.ceil(data.length / CHUNK_SIZE);

  console.log('[playback] Sending: ' + width + 'x' + height +
              ', ' + data.length + ' bytes, ' + totalChunks + ' chunks');

  Pebble.sendAppMessage({
    'ImageWidth': width,
    'ImageHeight': height,
    'ImageDataSize': data.length,
    'ImageChunksTotal': totalChunks
  }, function() {
    console.log('[playback] Header sent');
    sendChunk(data, 0, totalChunks);
  }, function() {
    console.log('[playback] Header failed');
    transferInProgress = false;
    sendError('Header send failed');
  });
}

function sendChunk(data, index, totalChunks) {
  var start = index * CHUNK_SIZE;
  var end = Math.min(start + CHUNK_SIZE, data.length);

  var chunk = [];
  for (var i = start; i < end; i++) {
    chunk.push(data[i]);
  }

  Pebble.sendAppMessage({
    'ImageChunkIndex': index,
    'ImageChunkData': chunk
  }, function() {
    console.log('[playback] Chunk ' + (index + 1) + '/' + totalChunks);
    if (index + 1 < totalChunks) {
      sendChunk(data, index + 1, totalChunks);
    } else {
      console.log('[playback] Transfer complete');
      transferInProgress = false;
    }
  }, function() {
    console.log('[playback] Chunk ' + index + ' failed, retrying');
    setTimeout(function() {
      Pebble.sendAppMessage({
        'ImageChunkIndex': index,
        'ImageChunkData': chunk
      }, function() {
        if (index + 1 < totalChunks) {
          sendChunk(data, index + 1, totalChunks);
        } else {
          console.log('[playback] Transfer complete');
          transferInProgress = false;
        }
      }, function() {
        console.log('[playback] Chunk ' + index + ' retry failed');
        transferInProgress = false;
        sendError('Transfer failed');
      });
    }, 500);
  });
}

module.exports = {
  sendImageFromUrl: downloadAndSend,
  isTransferring: function() { return transferInProgress; }
};
