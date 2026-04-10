var auth = require('./spotify_auth');
var api = require('./spotify_api');
var imageTransfer = require('./image_transfer');

var pollTimer = null;
var POLL_INTERVAL = 5000;
var currentVolume = 50;
var lastTrackId = null;
var lastImageUrl = null;
var listSendInProgress = false;
var lastFetchedItems = [];

// Target album-art size per platform — used to pick the smallest
// Spotify image variant that's still big enough to cover the display
// without upscaling. Smaller source JPEG = less to download, less to
// decode in jpeg-js (which is the single biggest cost on the phone
// side).
function getTargetArtSize() {
  try {
    var platform = (Pebble.getActiveWatchInfo().platform || '');
    if (platform === 'gabbro') return 260;
    if (platform === 'chalk')  return 180;
    if (platform === 'emery')  return 200;
    if (platform === 'basalt') return 168;
  } catch (e) {}
  return 168;
}

// Spotify returns album.images sorted largest-first, usually
// [640, 300, 64]. Pick the smallest image whose longest side is still
// >= the target display size; fall back to the largest if none qualify.
function pickImageUrl(images, target) {
  if (!images || images.length === 0) return null;
  var best = null;
  for (var i = 0; i < images.length; i++) {
    var img = images[i];
    var side = Math.max(img.width || 0, img.height || 0);
    if (side >= target) {
      if (!best || side < Math.max(best.width || 0, best.height || 0)) {
        best = img;
      }
    }
  }
  return (best || images[0]).url;
}

function noop() {}

function sendError(msg) {
  console.log('[playback] ERROR: ' + msg);
  try {
    Pebble.sendAppMessage({ 'ErrorMsg': msg }, null, null);
  } catch (e) { /* best effort */ }
}

function sendAuthStatus() {
  var authed = auth.isAuthenticated() ? 1 : 0;
  Pebble.sendAppMessage({ 'AuthStatus': authed }, null, null);
}

// --- Now Playing ---

function fetchNowPlaying() {
  if (listSendInProgress) return; // Don't collide with list transfer
  api.getCurrentPlayback(function(err, data) {
    if (err) {
      console.log('[playback] Now playing error: ' + err.message);
      // Don't stop polling — next cycle will retry (and refresh token if needed)
      return;
    }
    if (!data || !data.item) {
      // No active playback
      return;
    }

    var track = data.item;
    var artistNames = [];
    if (track.artists) {
      for (var i = 0; i < track.artists.length; i++) {
        artistNames.push(track.artists[i].name);
      }
    }

    // Kick off the art download BEFORE we send track metadata over
    // AppMessage. The HTTP XHR and JPEG decode happen asynchronously on
    // the phone, so they run in parallel with the watch receiving the
    // text info and any subsequent BLE traffic. image_transfer itself
    // de-dupes against lastUrl, and we also skip the whole step if the
    // album art URL hasn't changed.
    if (track.album && track.album.images && track.album.images.length > 0) {
      var imageUrl = pickImageUrl(track.album.images, getTargetArtSize());
      if (imageUrl && imageUrl !== lastImageUrl && !imageTransfer.isTransferring()) {
        lastImageUrl = imageUrl;
        imageTransfer.sendImageFromUrl(imageUrl);
      }
    }
    lastTrackId = track.id;

    Pebble.sendAppMessage({
      'TrackTitle': track.name || '',
      'TrackArtist': artistNames.join(', '),
      'TrackDuration': Math.floor(track.duration_ms / 1000),
      'TrackElapsed': Math.floor((data.progress_ms || 0) / 1000),
      'TrackIsPlaying': data.is_playing ? 1 : 0
    }, null, null);

    // Update volume tracking
    if (data.device) {
      currentVolume = data.device.volume_percent;
    }
  });
}

function startPolling() {
  if (pollTimer) return;
  fetchNowPlaying();
  pollTimer = setInterval(fetchNowPlaying, POLL_INTERVAL);
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

// --- List sending ---

function sendListItem(listType, items, index, count) {
  if (index >= count) {
    listSendInProgress = false;
    Pebble.sendAppMessage({
      'ListType': listType,
      'ListDone': 1
    }, null, null);
    return;
  }

  var item = items[index];
  Pebble.sendAppMessage({
    'ListType': listType,
    'ListCount': count,
    'ListIndex': index,
    'ListItemTitle': item.title || '',
    'ListItemSubtitle': item.subtitle || '',
    'ListItemUri': item.uri || ''
  }, function() {
    sendListItem(listType, items, index + 1, count);
  }, function() {
    // Retry once after delay
    setTimeout(function() {
      sendListItem(listType, items, index, count);
    }, 500);
  });
}

function fetchAndSendList(listType, apiFn, formatFn) {
  apiFn(function(err, data) {
    if (err) {
      sendError(err.message);
      if (err.message === 'Token expired') {
        sendAuthStatus();
      }
      return;
    }
    if (!data) {
      sendError('No data');
      return;
    }
    var items = formatFn(data);
    lastFetchedItems = items;
    var count = Math.min(items.length, 20);
    if (count === 0) {
      Pebble.sendAppMessage({
        'ListType': listType,
        'ListDone': 1
      }, null, null);
      return;
    }
    listSendInProgress = true;
    sendListItem(listType, items, 0, count);
  });
}

function formatPlaylists(data) {
  var items = [];
  var list = data.items || [];
  for (var i = 0; i < list.length; i++) {
    items.push({
      title: list[i].name,
      subtitle: list[i].owner ? list[i].owner.display_name : '',
      uri: list[i].uri
    });
  }
  return items;
}

function formatTracks(data) {
  var items = [];
  var list = data.items || [];
  for (var i = 0; i < list.length; i++) {
    var track = list[i].track || list[i];
    var artistName = (track.artists && track.artists.length > 0) ? track.artists[0].name : '';
    items.push({
      title: track.name,
      subtitle: artistName,
      uri: track.uri
    });
  }
  return items;
}

function formatArtists(data) {
  var items = [];
  var list = (data.artists && data.artists.items) || [];
  for (var i = 0; i < list.length; i++) {
    items.push({
      title: list[i].name,
      subtitle: (list[i].genres && list[i].genres.length > 0) ? list[i].genres[0] : '',
      uri: list[i].uri
    });
  }
  return items;
}

function formatAlbums(data) {
  var items = [];
  var list = data.items || [];
  for (var i = 0; i < list.length; i++) {
    var album = list[i].album || list[i];
    var artistName = (album.artists && album.artists.length > 0) ? album.artists[0].name : '';
    items.push({
      title: album.name,
      subtitle: artistName,
      uri: album.uri
    });
  }
  return items;
}

// --- Command dispatch ---

function handleCommand(cmd, context) {
  switch (cmd) {
    case 1: // CMD_FETCH_NOW_PLAYING
      // Just refresh metadata. Don't wipe the art dedupe keys — the
      // watch fires this on startup (main.c) AND every time the user
      // opens the Now Playing menu (menu.c), and clearing the dedupe
      // made us re-download the same cover on top of the one the poll
      // loop had just kicked off. If the track actually changed,
      // fetchNowPlaying's normal URL comparison will catch it.
      fetchNowPlaying();
      break;
    case 2: // CMD_FETCH_PLAYLISTS
      fetchAndSendList(0, api.getPlaylists, formatPlaylists);
      break;
    case 3: // CMD_FETCH_ARTISTS
      fetchAndSendList(1, api.getFollowedArtists, formatArtists);
      break;
    case 4: // CMD_FETCH_ALBUMS
      fetchAndSendList(2, api.getSavedAlbums, formatAlbums);
      break;
    case 5: // CMD_FETCH_LIKED_SONGS
      fetchAndSendList(3, api.getLikedTracks, formatTracks);
      break;
    case 15: // CMD_LIKE_TRACK
      api.getCurrentPlayback(function(err, data) {
        if (data && data.item) {
          console.log('[playback] Liking track: ' + data.item.name + ' (' + data.item.id + ')');
          api.saveTrack(data.item.id, function(err) {
            if (err) console.log('[playback] Like failed: ' + err.message);
            else console.log('[playback] Liked successfully');
          });
        }
      });
      break;
    case 16: // CMD_DISLIKE_TRACK
      api.getCurrentPlayback(function(err, data) {
        if (data && data.item) {
          console.log('[playback] Disliking track: ' + data.item.name + ' (' + data.item.id + ')');
          api.removeTrack(data.item.id, function(err) {
            if (err) console.log('[playback] Dislike failed: ' + err.message);
            else console.log('[playback] Disliked successfully');
          });
        }
      });
      break;
    case 10: // CMD_PLAY_PAUSE
      api.getCurrentPlayback(function(err, data) {
        if (err) { sendError(err.message); return; }
        startPolling(); // Restart polling if it was stopped
        if (data && data.is_playing) {
          api.pause(function() { setTimeout(fetchNowPlaying, 300); });
        } else {
          api.play(function() { setTimeout(fetchNowPlaying, 300); });
        }
      });
      break;
    case 11: // CMD_NEXT_TRACK
      api.nextTrack(function() { startPolling(); setTimeout(fetchNowPlaying, 500); });
      break;
    case 12: // CMD_PREV_TRACK
      api.previousTrack(function() { startPolling(); setTimeout(fetchNowPlaying, 500); });
      break;
    case 13: // CMD_VOLUME_UP
      currentVolume = Math.min(100, currentVolume + 10);
      api.setVolume(currentVolume, noop);
      break;
    case 14: // CMD_VOLUME_DOWN
      currentVolume = Math.max(0, currentVolume - 10);
      api.setVolume(currentVolume, noop);
      break;
    case 20: // CMD_PLAY_CONTEXT
      if (context) {
        if (context.indexOf(':track:') !== -1) {
          // It's a single track (likely from Liked Songs list)
          // To make "Next" work, we send this track and all following ones in the current list
          var uris = [];
          var found = false;
          for (var i = 0; i < lastFetchedItems.length; i++) {
            if (lastFetchedItems[i].uri === context) found = true;
            if (found) uris.push(lastFetchedItems[i].uri);
          }
          
          if (uris.length === 0) uris = [context]; // Fallback

          api.playContext(null, function(err) {
            if (err) { sendError(err.message); return; }
            setTimeout(fetchNowPlaying, 500);
          }, { uris: uris });
        } else {
          // Play a regular context (playlist/album/artist)
          api.playContext(context, function(err) {
            if (err) { sendError(err.message); return; }
            setTimeout(fetchNowPlaying, 500);
          });
        }
      }
      break;
    case 30: // CMD_FETCH_ART
      if (context) {
        imageTransfer.sendImageFromUrl(context);
      }
      break;
  }
}

// --- Auth change handler ---

function onAuthChange(authenticated) {
  sendAuthStatus();
  if (authenticated) {
    startPolling();
  } else {
    stopPolling();
  }
}

// --- Init ---

auth.init(onAuthChange);

Pebble.addEventListener('ready', function() {
  console.log('[playback] JS ready');
  Pebble.sendAppMessage({ 'JsReady': 1 }, function() {
    console.log('[playback] Ready signal sent');
  }, function() {
    console.log('[playback] Ready signal failed');
  });

  sendAuthStatus();

  if (auth.isAuthenticated()) {
    startPolling();
    // Force immediate fetch with small delay to ensure art loads on app start
    setTimeout(fetchNowPlaying, 1000);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('[playback] appmessage received');

  var cmd = e.payload['Command'] || e.payload[100];
  var ctx = e.payload['CommandContext'] || e.payload[101];

  if (cmd !== undefined && cmd !== null) {
    handleCommand(cmd, ctx);
    return;
  }
});
