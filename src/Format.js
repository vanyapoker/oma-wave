.pragma library
function time(ms) {
    const s = Math.max(0, Math.floor(ms / 1000))
    const m = Math.floor(s / 60), r = s % 60
    return m + ":" + (r < 10 ? "0" : "") + r
}
