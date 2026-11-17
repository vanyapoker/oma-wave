#!/usr/bin/python3
"""Concurrent JSON-lines bridge to YouTube Music and LRCLIB."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import sys
import threading

import requests

# Set by main, or injected by tests. Pure normalisers need neither dependency.
ytmusicapi = None
yt_dlp = None


def text(value, limit=300):
    return str(value or '')[:limit]


def thumbnail(obj):
    images = obj.get('thumbnails') or []
    return text(images[-1].get('url')) if images and isinstance(images[-1], dict) else ''


def names(value):
    if isinstance(value, str):
        return [text(value)] if value else []
    return [text(x.get('name') if isinstance(x, dict) else x) for x in (value or [])[:200] if x]


def duration_ms(obj):
    seconds = obj.get('duration_seconds')
    if seconds is not None:
        try:
            return max(0, int(float(seconds) * 1000))
        except (TypeError, ValueError, OverflowError):
            pass
    duration = obj.get('duration') or ''
    try:
        seconds = 0
        for part in duration.split(':'):
            seconds = seconds * 60 + int(part)
        return max(0, seconds * 1000)
    except (ValueError, AttributeError):
        return 0


def track(obj):
    video_id = text(obj.get('videoId'))
    if not video_id:
        return None
    album = obj.get('album') or {}
    if not isinstance(album, dict):
        album = {'name': album}
    return {'videoId': video_id, 'title': text(obj.get('title')), 'artists': names(obj.get('artists')),
            'album': text(album.get('name')), 'albumId': text(album.get('id') or album.get('browseId')),
            'durationMs': duration_ms(obj), 'thumb': thumbnail(obj)}


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
    subtitle = obj.get('subtitle') or ', '.join(artists) or obj.get('author') or ''
    if isinstance(subtitle, dict):
        subtitle = subtitle.get('name', '')
    return {'kind': kind, 'id': text(identifier), 'title': text(obj.get('title') or obj.get('name')),
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
            author = obj.get('author') or ''
            if isinstance(author, dict):
                author = author.get('name', '')
            subtitle = '' if method == 'artist' else text(obj.get('subtitle') or ', '.join(artists) or author)
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
