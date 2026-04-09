var auth = require('./spotify_auth');

var BASE_URL = 'https://api.spotify.com/v1';

function apiRequest(method, path, body, callback) {
  if (typeof body === 'function') {
    callback = body;
    body = null;
  }

  var token = auth.getToken();
  if (!token) {
    if (callback) callback(new Error('Not authenticated'), null);
    return;
  }

  // Auto-refresh if token is likely expired
  if (auth.isTokenExpired()) {
    auth.refreshAccessToken(function(err, newToken) {
      if (err) {
        if (callback) callback(err, null);
        return;
      }
      apiRequest(method, path, body, callback);
    });
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.open(method, BASE_URL + path, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + token);
  if (body) {
    xhr.setRequestHeader('Content-Type', 'application/json');
  }

  xhr.onload = function() {
    if (xhr.status === 401) {
      // Token rejected, attempt one refresh and retry
      auth.refreshAccessToken(function(err, newToken) {
        if (err) {
          // Don't clear tokens here — let the user re-login manually
          if (callback) callback(new Error('Token expired'), null);
          return;
        }
        apiRequest(method, path, body, callback);
      });
      return;
    }
    if (xhr.status === 204) {
      // No content (e.g., no active device)
      if (callback) callback(null, null);
      return;
    }
    if (xhr.status < 200 || xhr.status >= 300) {
      if (callback) callback(new Error('API error ' + xhr.status), null);
      return;
    }
    try {
      var data = JSON.parse(xhr.responseText);
      if (callback) callback(null, data);
    } catch (e) {
      if (callback) callback(e, null);
    }
  };

  xhr.onerror = function() {
    if (callback) callback(new Error('Network error'), null);
  };

  xhr.send(body ? JSON.stringify(body) : null);
}

module.exports = {
  getCurrentPlayback: function(cb) {
    apiRequest('GET', '/me/player', cb);
  },

  getPlaylists: function(cb) {
    apiRequest('GET', '/me/playlists?limit=20', cb);
  },

  getFollowedArtists: function(cb) {
    apiRequest('GET', '/me/following?type=artist&limit=20', cb);
  },

  getSavedAlbums: function(cb) {
    apiRequest('GET', '/me/albums?limit=20', cb);
  },

  getLikedTracks: function(cb) {
    apiRequest('GET', '/me/tracks?limit=20', cb);
  },

  play: function(cb) {
    apiRequest('PUT', '/me/player/play', cb);
  },

  pause: function(cb) {
    apiRequest('PUT', '/me/player/pause', cb);
  },

  nextTrack: function(cb) {
    apiRequest('POST', '/me/player/next', cb);
  },

  previousTrack: function(cb) {
    apiRequest('POST', '/me/player/previous', cb);
  },

  setVolume: function(percent, cb) {
    apiRequest('PUT', '/me/player/volume?volume_percent=' + percent, cb);
  },

  playContext: function(contextUri, cb, customBody) {
    var body = customBody || { context_uri: contextUri };
    apiRequest('PUT', '/me/player/play', body, cb);
  },

  saveTrack: function(trackId, cb) {
    apiRequest('PUT', '/me/tracks?ids=' + trackId, cb);
  },

  removeTrack: function(trackId, cb) {
    apiRequest('DELETE', '/me/tracks?ids=' + trackId, cb);
  }
};
