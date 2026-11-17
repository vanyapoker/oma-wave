#!/usr/bin/python3
"""Concurrent JSON-lines bridge to YouTube Music and LRCLIB."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import sys
import random
import sqlite3
import threading
import time

import requests

# Set by main, or injected by tests. Pure normalisers need neither dependency.
ytmusicapi = None
yt_dlp = None


def text(value, limit=300):
    return str(value or '')[:limit]


def thumbnail(obj):
    images = obj.get('thumbnails') or obj.get('thumbnail') or []
    return text(images[-1].get('url')) if images and isinstance(images[-1], dict) else ''


def names(value):
    if isinstance(value, str):
        return [text(value)] if value else []
    return [text(x.get('name') if isinstance(x, dict) else x) for x in (value or [])[:200] if x]


def byline(value):
    """A subtitle/author field may be a string, one {'name'} dict or a list of them."""
    if isinstance(value, dict):
        value = [value]
    return ', '.join(names(value)) if isinstance(value, (list, tuple, str)) else ''


def duration_ms(obj):
    seconds = obj.get('duration_seconds')
    if seconds is not None:
        try:
            return max(0, int(float(seconds) * 1000))
        except (TypeError, ValueError, OverflowError):
            pass
    for key in ('duration', 'length'):
        value = obj.get(key) or ''
        try:
            seconds = 0
            for part in value.split(':'):
                seconds = seconds * 60 + int(part)
            return max(0, seconds * 1000)
        except (ValueError, AttributeError):
            continue
    return 0


TITLE_SPLIT = re.compile(r'\s+[-–—|]\s+')
TITLE_NOISE = re.compile(r'\s*[(\[][^)\]]*\b(?:official|video|audio|lyrics?|visuali[sz]er|clip|клип|mv|hd|4k|hq)\b[^)\]]*[)\]]', re.I)


def clean_title(title):
    cleaned = TITLE_NOISE.sub('', title)
    cleaned = cleaned.split(' | ', 1)[0].rstrip(' \t.-–—|').strip()
    return cleaned or title


def clean_video(title, artists):
    """Turn 'Artist - Title (Official Video)' uploads into (title, artists). Titles without a separator are kept."""
    parts = TITLE_SPLIT.split(title, maxsplit=1)
    if len(parts) != 2 or not parts[0].strip() or not parts[1].strip():
        return title, artists
    return clean_title(parts[1]), [parts[0].strip()]


def is_omv(obj):
    return obj.get('videoType') == 'MUSIC_VIDEO_TYPE_OMV'


def clean_official(title, artists):
    """Official music videos: strip noise, and a leading 'Artist - ' when it matches a credited artist."""
    parts = TITLE_SPLIT.split(title, maxsplit=1)
    if len(parts) == 2 and parts[0].strip() and parts[1].strip():
        left = parts[0].strip()
        low = left.lower()
        if any(low in a.lower() or a.lower() in low for a in artists):
            return clean_title(parts[1]), (artists if any(a.lower() == low for a in artists) else [left])
    return clean_title(title), artists


def is_video(obj):
    return obj.get('videoType') == 'MUSIC_VIDEO_TYPE_UGC' or (obj.get('resultType') or '').lower() == 'video'


def track(obj):
    video_id = text(obj.get('videoId'))
    if not video_id:
        return None
    album = obj.get('album') or {}
    if not isinstance(album, dict):
        album = {'name': album}
    title, artists = text(obj.get('title')), names(obj.get('artists'))
    if is_video(obj):
        title, artists = clean_video(title, artists)
    elif is_omv(obj):
        title, artists = clean_official(title, artists)
    return {'videoId': video_id, 'title': title, 'artists': artists,
            'album': text(album.get('name')), 'albumId': text(album.get('id') or album.get('browseId')),
            'durationMs': duration_ms(obj), 'thumb': thumbnail(obj)}


def from_track(obj):
    """Validate a Track dict as sent by the app (already normalised)."""
    if not isinstance(obj, dict) or not obj.get('videoId'):
        return None
    try:
        duration = max(0, int(obj.get('durationMs') or 0))
    except (TypeError, ValueError):
        duration = 0
    return {'videoId': text(obj['videoId']), 'title': text(obj.get('title')), 'artists': names(obj.get('artists')),
            'album': text(obj.get('album')), 'albumId': text(obj.get('albumId')),
            'durationMs': duration, 'thumb': text(obj.get('thumb'))}


def item(obj, kind=None):
    if kind is None:
        result_type = (obj.get('resultType') or '').lower()
        if result_type in ('song', 'video') or obj.get('videoId'):
            kind = 'song'
        elif result_type in ('album', 'single', 'ep') or str(obj.get('browseId') or '').startswith('MPRE'):
            kind = 'album'
        elif result_type == 'playlist' or obj.get('playlistId'):
            kind = 'playlist'
        elif result_type == 'artist' or str(obj.get('browseId') or '').startswith('UC'):
            kind = 'artist'
        else:
            return None
    key = {'song': 'videoId', 'album': 'browseId', 'playlist': 'playlistId', 'artist': 'channelId'}[kind]
    identifier = obj.get(key)
    if kind == 'artist':
        identifier = identifier or obj.get('browseId')
    if not identifier:
        return None
    artists = names(obj.get('artists'))
    subtitle = byline(obj.get('subtitle')) or ', '.join(artists) or byline(obj.get('author'))
    title = text(obj.get('title') or obj.get('name'))
    if kind == 'song' and is_video(obj):
        cleaned, cleaned_artists = clean_video(title, artists)
        if cleaned_artists != artists:
            title, subtitle = cleaned, cleaned_artists[0]
    elif kind == 'song' and is_omv(obj):
        cleaned, cleaned_artists = clean_official(title, artists)
        if cleaned_artists != artists:
            subtitle = ', '.join(cleaned_artists)
        title = cleaned
    return {'kind': kind, 'id': text(identifier), 'title': title,
            'subtitle': text(subtitle), 'thumb': thumbnail(obj)}


def mapped(values, fn):
    result = []
    for obj in values or []:
        if isinstance(obj, dict):
            value = fn(obj)
            if value is not None:
                result.append(value)
                if len(result) == 200:
                    break
    return result


def parse_lyrics(data):
    synced = data.get('syncedLyrics')
    if synced:
        lines = []
        pattern = re.compile(r'\[(\d+):(\d{2})(?:\.(\d+))?\]')
        for line in synced.splitlines():
            tags = list(pattern.finditer(line))
            lyric = text(pattern.sub('', line).strip(), 500)
            for tag in tags:
                fraction = (tag.group(3) or '')[:3].ljust(3, '0')
                lines.append({'t': (int(tag.group(1)) * 60 + int(tag.group(2))) * 1000 + int(fraction), 'text': lyric})
        lines.sort(key=lambda x: x['t'])
        return {'state': 'synced', 'lines': lines[:2000]}
    plain = data.get('plainLyrics')
    if plain:
        return {'state': 'plain', 'lines': [{'t': -1, 'text': text(line, 500)} for line in plain.splitlines()[:2000]]}
    return {'state': 'missing', 'lines': []}


def fetch_lyrics(params):
    query = {'track_name': params.get('title', ''), 'artist_name': params.get('artist', '')}
    # Search results often lack album and length; LRCLIB matches without them.
    if params.get('album'):
        query['album_name'] = params['album']
    if int(params.get('durationSec') or 0) > 0:
        query['duration'] = int(params['durationSec'])
    response = requests.get('https://lrclib.net/api/get', params=query,
        headers={'User-Agent': 'omawave/0.1.0'}, timeout=8, stream=True)
    with response:
        if response.status_code in (400, 404):
            return {'state': 'missing', 'lines': []}
        response.raise_for_status()
        body = bytearray()
        for chunk in response.iter_content(chunk_size=65536):
            body.extend(chunk)
            if len(body) > 1024 * 1024:
                return {'state': 'missing', 'lines': []}
        return parse_lyrics(json.loads(body))


MOODS = {'any': None, 'energetic': 'Energize', 'happy': 'Feel good', 'calm': 'Chill', 'sad': 'Sad'}
ACTIVITIES = {'any': None, 'wakeup': 'Feel good', 'commute': 'Commute', 'work': 'Focus', 'workout': 'Workout', 'sleep': 'Sleep'}
CHARACTERS = ('favourite', 'discover', 'popular')
AFFINITY = {'favourite': 0.8, 'discover': 0.2, 'popular': 0.3}
KNOWN_BONUS = {'favourite': 0.4, 'discover': -1.5, 'popular': 0}


def classify(listened_ms, duration_ms, interrupted):
    if (duration_ms > 0 and listened_ms >= 0.7 * duration_ms) or (duration_ms == 0 and listened_ms >= 120000):
        return 'complete'
    if not interrupted and listened_ms < 30000:
        return 'skip'
    return 'partial'


class Taste:
    """Local listen log and ratings."""

    def __init__(self, path):
        self.lock = threading.Lock()
        self.db = sqlite3.connect(str(path), timeout=5, check_same_thread=False)
        with self.lock, self.db:
            self.db.executescript(
                'CREATE TABLE IF NOT EXISTS plays(id INTEGER PRIMARY KEY, video_id TEXT NOT NULL, track TEXT NOT NULL, '
                'at INTEGER NOT NULL, listened_ms INTEGER NOT NULL, duration_ms INTEGER NOT NULL, '
                'outcome TEXT NOT NULL, source TEXT NOT NULL);'
                'CREATE INDEX IF NOT EXISTS plays_video ON plays(video_id);'
                'CREATE TABLE IF NOT EXISTS ratings(video_id TEXT PRIMARY KEY, track TEXT NOT NULL, '
                'rating TEXT NOT NULL, at INTEGER NOT NULL);')
        os.chmod(path, 0o600)

    def log_play(self, trk, listened_ms, duration_ms, source, interrupted):
        outcome = classify(listened_ms, duration_ms, interrupted)
        with self.lock, self.db:
            self.db.execute('INSERT INTO plays(video_id, track, at, listened_ms, duration_ms, outcome, source) VALUES(?,?,?,?,?,?,?)',
                            (trk['videoId'], json.dumps(trk), int(time.time()), listened_ms, duration_ms, outcome, source))
            self.db.execute('DELETE FROM plays WHERE id <= (SELECT MAX(id) - 20000 FROM plays)')
        return outcome

    def rate(self, trk, rating):
        with self.lock, self.db:
            if rating == 'none':
                self.db.execute('DELETE FROM ratings WHERE video_id = ?', (trk['videoId'],))
            else:
                self.db.execute('INSERT OR REPLACE INTO ratings(video_id, track, rating, at) VALUES(?,?,?,?)',
                                (trk['videoId'], json.dumps(trk), rating, int(time.time())))

    def rating(self, video_id):
        with self.lock:
            row = self.db.execute('SELECT rating FROM ratings WHERE video_id = ?', (video_id,)).fetchone()
        return row[0] if row else 'none'

    def profile(self):
        with self.lock:
            plays = self.db.execute('SELECT video_id, track, at, outcome, source FROM plays ORDER BY id').fetchall()
            ratings = self.db.execute('SELECT video_id, track, rating, at FROM ratings').fetchall()
        now = time.time()
        artist_score, artist_dislikes = {}, {}
        tracks, video_score, skips = {}, {}, {}
        known, recent, disliked = set(), set(), set()
        seed, seed_at = None, 0

        def artists_of(trk):
            return [a.lower() for a in trk.get('artists', [])]

        for video_id, raw, at, outcome, source in plays:
            trk = json.loads(raw)
            tracks[video_id] = trk
            known.add(video_id)
            if now - at < 3 * 3600:
                recent.add(video_id)
            delta = {'complete': 1, 'skip': -1}.get(outcome, 0)
            video_score[video_id] = video_score.get(video_id, 0) + delta
            if outcome == 'skip':
                skips[video_id] = skips.get(video_id, 0) + 1
            for artist in artists_of(trk):
                artist_score[artist] = artist_score.get(artist, 0) + delta
            if source == 'wave' and outcome == 'complete' and now - at < 1800 and at >= seed_at:
                seed, seed_at = trk, at
        for video_id, raw, rating, at in ratings:
            trk = json.loads(raw)
            tracks.setdefault(video_id, trk)
            if rating == 'like':
                video_score[video_id] = video_score.get(video_id, 0) + 3
                for artist in artists_of(trk):
                    artist_score[artist] = artist_score.get(artist, 0) + 3
                if now - at < 1800 and at >= seed_at:
                    seed, seed_at = trk, at
            elif rating == 'dislike':
                disliked.add(video_id)
                for artist in artists_of(trk):
                    artist_score[artist] = artist_score.get(artist, 0) - 4
                    artist_dislikes[artist] = artist_dislikes.get(artist, 0) + 1
        favourites = sorted((v for v, s in video_score.items() if s > 0 and v not in disliked), key=lambda v: -video_score[v])[:30]
        return {'artist_score': artist_score, 'artist_dislikes': artist_dislikes, 'disliked': disliked,
                'skipped2': {v for v, n in skips.items() if n >= 2}, 'known': known, 'recent': recent,
                'favourites': [tracks[v] for v in favourites], 'session_seed': seed}


class WorkerError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


class Worker:
    def __init__(self, config_dir):
        self.directory = Path(config_dir)
        self.directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.auth = self.directory / 'auth.json'
        self.cookies = self.directory / 'cookies.txt'
        self.lock = threading.RLock()
        self.signed_in = False
        self.taste = Taste(self.directory / 'taste.sqlite3')
        self.random = random.Random()
        self._categories = None
        self._remote = None
        try:
            self.client = ytmusicapi.YTMusic(auth=str(self.auth)) if self.auth.exists() else ytmusicapi.YTMusic()
            self.signed_in = self.auth.exists()
        except Exception as exc:
            print('Invalid saved authentication: ' + str(exc), file=sys.stderr)
            self.clear_auth()
            self.client = ytmusicapi.YTMusic()

    def clear_auth(self):
        for path in (self.auth, self.cookies):
            path.unlink(missing_ok=True)

    def sign_in(self, browser):
        with self.lock:
            jar = yt_dlp.cookies.extract_cookies_from_browser(browser)
            cookies = [c for c in jar if c.domain.lstrip('.').lower() == 'youtube.com' or c.domain.lower().endswith('.youtube.com')]
            if not any(c.name == '__Secure-3PAPISID' for c in cookies):
                raise WorkerError('no_session', f'No YouTube session in {browser}. Sign in at music.youtube.com there first.')
            auth_tmp = self.auth.with_suffix('.json.tmp')
            cookies_tmp = self.cookies.with_suffix('.txt.tmp')
            try:
                raw = 'cookie: ' + '; '.join(c.name + '=' + c.value for c in cookies)
                raw += '\nx-goog-authuser: 0\nauthorization: SAPISIDHASH 0_0\nx-origin: https://music.youtube.com'
                headers = ytmusicapi.setup(headers_raw=raw)
                fd = os.open(auth_tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                with os.fdopen(fd, 'w', encoding='utf-8') as out:
                    os.fchmod(out.fileno(), 0o600)
                    out.write(headers)
                output_jar = yt_dlp.cookies.YoutubeDLCookieJar(str(cookies_tmp))
                for cookie in cookies:
                    output_jar.set_cookie(cookie)
                # Create privately before the jar opens it for writing.
                fd = os.open(cookies_tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                os.fchmod(fd, 0o600)
                os.close(fd)
                output_jar.save()
                os.replace(auth_tmp, self.auth)
                os.replace(cookies_tmp, self.cookies)
                client = ytmusicapi.YTMusic(auth=str(self.auth))
                client.get_library_playlists(limit=1)
                self.client = client
                self.signed_in = True
            except Exception as exc:
                self.clear_auth()
                self.client = ytmusicapi.YTMusic()
                self.signed_in = False
                raise WorkerError('sign_in_failed', str(exc)) from exc
            finally:
                auth_tmp.unlink(missing_ok=True)
                cookies_tmp.unlink(missing_ok=True)
            return {'signedIn': True}

    def mood_params(self, client, title):
        if self._categories is None:
            self._categories = client.get_mood_categories()
        for entry in self._categories.get('Moods & moments', []):
            if (entry.get('title') or '').lower() == title.lower():
                return entry.get('params')
        return None

    def remote_seeds(self, client):
        cached = self._remote
        if cached and time.time() - cached[0] < 600:
            return cached[1]
        tracks = []
        try:
            tracks = mapped(client.get_history()[:20], track) + mapped(client.get_liked_songs(limit=50).get('tracks'), track)
        except Exception as exc:
            print('Remote seeds failed: ' + str(exc), file=sys.stderr)
        self._remote = (time.time(), tracks)
        return tracks

    def wave_next(self, client, signed_in, params):
        mood, activity, character = (text(params.get(k) or d) for k, d in (('mood', 'any'), ('activity', 'any'), ('character', 'favourite')))
        if mood not in MOODS or activity not in ACTIVITIES or character not in CHARACTERS:
            raise WorkerError('bad_request', 'Unknown wave setting.')
        exclude = {text(x) for x in (params.get('exclude') or [])[:500]}
        try:
            count = max(1, min(25, int(params.get('count', 10))))
        except (TypeError, ValueError):
            count = 10
        profile = self.taste.profile()
        rng = self.random
        candidates, last_error = {}, None

        def add(trk, weight, source):
            if trk is None:
                return
            old = candidates.get(trk['videoId'])
            if old is None or old[1] < weight:
                candidates[trk['videoId']] = (trk, weight, source)

        def attempt(fn):
            nonlocal last_error
            try:
                return fn()
            except Exception as exc:
                last_error = exc
                print('Wave source failed: ' + str(exc), file=sys.stderr)
                return None

        pool = list({t['videoId']: t for t in profile['favourites'] + (self.remote_seeds(client) if signed_in else [])}.values())
        seeds = []
        if profile['session_seed']:
            seeds.append(profile['session_seed']['videoId'])
        picks = {'favourite': 2, 'discover': 1, 'popular': 0}[character]
        for trk in rng.sample(pool, min(picks, len(pool))):
            if trk['videoId'] not in seeds:
                seeds.append(trk['videoId'])
        tuned = mood != 'any' or activity != 'any'
        for seed in seeds:
            tracks = attempt(lambda: mapped(client.get_watch_playlist(videoId=seed, radio=True, limit=30).get('tracks'), track))
            for trk in tracks or []:
                if trk['videoId'] != seed:
                    add(trk, 0.5 if tuned else 1.0, 'radio')
        if tuned or character == 'popular' or not seeds:
            titles = []
            for title in (MOODS[mood], ACTIVITIES[activity]):
                if title and title not in titles:
                    titles.append(title)
            for title in titles or ['Feel good']:
                def mood_tracks(title=title):
                    mood_params = self.mood_params(client, title)
                    if not mood_params:
                        return []
                    playlists = [p for p in client.get_mood_playlists(mood_params)[:10] if p.get('playlistId')]
                    if not playlists:
                        return []
                    chosen = rng.choice(playlists)
                    return mapped(client.get_watch_playlist(playlistId=chosen['playlistId'], limit=50).get('tracks'), track)
                for trk in attempt(mood_tracks) or []:
                    add(trk, 1.0, 'mood')
        if character == 'favourite':
            for trk in profile['favourites']:
                add(trk, 0.7, 'favourite')
        if not candidates and last_error is not None:
            raise last_error

        scored = []
        for video_id, (trk, weight, source) in candidates.items():
            artists = [a.lower() for a in trk['artists']]
            if video_id in profile['disliked'] or video_id in profile['skipped2'] or video_id in profile['recent'] or video_id in exclude:
                continue
            if any(profile['artist_dislikes'].get(a, 0) >= 2 for a in artists):
                continue
            affinity = max(-5, min(5, sum(profile['artist_score'].get(a, 0) for a in artists))) / 5
            score = (weight + AFFINITY[character] * affinity + KNOWN_BONUS[character] * (video_id in profile['known'])
                     + (0.5 if character == 'popular' and source == 'mood' else 0) + rng.uniform(0, 0.3))
            scored.append((score, trk))
        scored.sort(key=lambda x: -x[0])
        result, per_artist = [], {}
        for _, trk in scored:
            key = trk['artists'][0].lower() if trk['artists'] else trk['videoId']
            if per_artist.get(key, 0) >= 2:
                continue
            per_artist[key] = per_artist.get(key, 0) + 1
            result.append(trk)
            if len(result) == count:
                break
        return {'tracks': result}

    def rate(self, trk, rating, client, signed_in):
        self.taste.rate(trk, rating)
        if not signed_in:
            return False
        try:
            from ytmusicapi.models.content.enums import LikeStatus
            status = {'like': LikeStatus.LIKE, 'dislike': LikeStatus.DISLIKE, 'none': LikeStatus.INDIFFERENT}[rating]
            client.rate_song(trk['videoId'], status)
            return True
        except Exception as exc:
            print('Rating sync failed: ' + str(exc), file=sys.stderr)
            return False

    def call(self, method, params):
        if method == 'signIn':
            return self.sign_in(text(params.get('browser') or 'chromium'))
        if method == 'signOut':
            with self.lock:
                self.clear_auth()
                self.client = ytmusicapi.YTMusic()
                self.signed_in = False
            return {'signedIn': False}
        if method == 'lyrics':
            return fetch_lyrics(params)
        if method == 'logPlay':
            trk = params.get('track') or {}
            video_id = text(trk.get('videoId')) if isinstance(trk, dict) else ''
            listened, interrupted = max(0, int(params.get('listenedMs') or 0)), bool(params.get('interrupted'))
            if not video_id or (listened < 1000 and interrupted):
                return {'outcome': 'ignored'}
            source = 'wave' if params.get('source') == 'wave' else 'other'
            clean = [from_track(trk)]
            if clean[0] is None:
                return {'outcome': 'ignored'}
            return {'outcome': self.taste.log_play(clean[0], listened, max(0, int(params.get('durationMs') or 0)), source, interrupted)}
        if method == 'rating':
            return {'rating': self.taste.rating(text(params.get('videoId')))}
        if method in ('rate', 'waveNext'):
            with self.lock:
                client, signed_in = self.client, self.signed_in
            if method == 'waveNext':
                return self.wave_next(client, signed_in, params)
            rating = params.get('rating')
            clean = [from_track(params.get('track'))]
            if rating not in ('like', 'dislike', 'none') or clean[0] is None:
                raise WorkerError('bad_request', 'Bad rating request.')
            return {'rating': rating, 'synced': self.rate(clean[0], rating, client, signed_in)}
        with self.lock:
            client, signed_in = self.client, self.signed_in
        if method in ('libraryPlaylists', 'libraryAlbums', 'libraryArtists', 'likedSongs') and not signed_in:
            raise WorkerError('not_signed_in', 'Sign in to access your library.')
        if method == 'home':
            sections = client.get_home(limit=6)
            return {'sections': [{'title': text(s.get('title')), 'items': mapped(s.get('contents'), item)} for s in sections[:200]]}
        if method == 'search':
            results = client.search(params.get('query', ''), limit=20)
            top = next((item(r) for r in results if r.get('category') == 'Top result' and item(r)), None)
            kinds = {'songs': ('song', 'video'), 'albums': ('album', 'single', 'ep'), 'artists': ('artist',), 'playlists': ('playlist',)}
            result = {'top': top}
            for key, types in kinds.items():
                result[key] = mapped([r for r in results if (r.get('resultType') or '').lower() in types], track if key == 'songs' else item)
            return result
        if method in ('album', 'playlist', 'artist'):
            identifier = text(params.get('id'))
            obj = (client.get_album(identifier) if method == 'album' else client.get_playlist(identifier, limit=200) if method == 'playlist' else client.get_artist(identifier))
            artists = names(obj.get('artists'))
            subtitle = '' if method == 'artist' else text(byline(obj.get('subtitle')) or ', '.join(artists) or byline(obj.get('author')))
            raw_tracks = obj.get('tracks') or obj.get('songs') or []
            if isinstance(raw_tracks, dict):
                raw_tracks = raw_tracks.get('results') or []
            result = {'kind': method, 'id': identifier, 'title': text(obj.get('title') or obj.get('name')), 'subtitle': subtitle, 'thumb': thumbnail(obj), 'tracks': mapped(raw_tracks, track)}
            if method == 'artist':
                for key in ('albums', 'singles'):
                    section = obj.get(key) or []
                    result[key] = mapped(section.get('results', []) if isinstance(section, dict) else section, lambda x: item(x, 'album'))
            return result
        if method == 'upNext':
            seed = params.get('videoId', '')
            return {'tracks': mapped([x for x in client.get_watch_playlist(videoId=seed, limit=25).get('tracks', []) if x.get('videoId') != seed], track)}
        if method == 'likedSongs':
            return {'tracks': mapped(client.get_liked_songs(limit=200).get('tracks'), track)}
        libraries = {'libraryPlaylists': ('get_library_playlists', 'playlist'), 'libraryAlbums': ('get_library_albums', 'album'), 'libraryArtists': ('get_library_subscriptions', 'artist')}
        if method in libraries:
            func, kind = libraries[method]
            return {'items': mapped(getattr(client, func)(limit=100), lambda x: item(x, kind))}
        raise WorkerError('bad_request', 'Unknown method: ' + text(method))


def serve(stdin, stdout, config_dir=None, worker=None):
    worker = worker or Worker(config_dir)
    output_lock = threading.Lock()
    def send(obj):
        with output_lock:
            stdout.write(json.dumps(obj, ensure_ascii=True, separators=(',', ':')) + '\n')
            stdout.flush()
    def handle(request):
        identifier = request.get('id') if isinstance(request, dict) else None
        try:
            if not isinstance(request, dict) or not isinstance(request.get('method'), str) or not isinstance(request.get('params', {}), dict):
                raise WorkerError('bad_request', 'Invalid request')
            result = worker.call(request['method'], request.get('params', {}))
            send({'id': identifier, 'result': result})
        except Exception as exc:
            code = exc.code if isinstance(exc, WorkerError) else 'network' if isinstance(exc, requests.exceptions.RequestException) else 'youtube_error'
            send({'id': identifier, 'error': {'code': code, 'message': text(str(exc))}})
    send({'id': 0, 'result': {'ready': True, 'signedIn': worker.signed_in}})
    with ThreadPoolExecutor(max_workers=4) as pool:
        for line in stdin:
            try:
                request = json.loads(line)
            except (ValueError, TypeError):
                send({'id': None, 'error': {'code': 'bad_request', 'message': 'Invalid JSON'}})
                continue
            pool.submit(handle, request)


def main():
    global ytmusicapi, yt_dlp
    parser = argparse.ArgumentParser()
    parser.add_argument('--config-dir', required=True)
    args = parser.parse_args()
    try:
        import ytmusicapi as ytmusic_module
        ytmusicapi = ytmusic_module
    except ImportError:
        print('missing python module: ytmusicapi', file=sys.stderr)
        return 3
    try:
        import yt_dlp as yt_dlp_module
        import yt_dlp.cookies
        yt_dlp = yt_dlp_module
    except ImportError:
        print('missing python module: yt_dlp', file=sys.stderr)
        return 3
    serve(sys.stdin, sys.stdout, args.config_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
