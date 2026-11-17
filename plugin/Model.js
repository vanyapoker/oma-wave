function formatTime(ms) {
  var seconds = Math.floor(Math.max(0, Number(ms) || 0) / 1000)
  var hours = Math.floor(seconds / 3600)
  var minutes = Math.floor(seconds / 60) % 60
  var remainder = seconds % 60
  return (hours ? hours + ":" + (minutes < 10 ? "0" : "") : "") + minutes + ":" + (remainder < 10 ? "0" : "") + remainder
}

function parseLevels(arr) {
  if (!Array.isArray(arr) || arr.length !== 8) return null
  var result = []
  for (var i = 0; i < 8; ++i) {
    if (typeof arr[i] !== "number" || !isFinite(arr[i])) return null
    result.push(Math.max(0, Math.min(100, Math.round(arr[i]))))
  }
  return result
}

function lyricIndex(lines, ms) {
  var low = 0, high = lines ? lines.length : 0
  while (low < high) {
    var mid = Math.floor((low + high) / 2)
    if (lines[mid].t <= ms) low = mid + 1
    else high = mid
  }
  return low - 1
}

function trackLabel(track) {
  if (!track) return ""
  var artists = Array.isArray(track.artists) ? track.artists.join(", ") : ""
  return (track.title || "") + (artists ? " · " + artists : "")
}
