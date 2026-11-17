import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('catalog_worker', Path(__file__).parents[1] / 'catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


class FakeClient:
    def __init__(self, auth=None):
        self.auth = auth

    def get_library_playlists(self, limit):
        return []


class FakeJar:
    def __init__(self, filename):
        self.filename = filename
        self.cookies = []

    def set_cookie(self, cookie):
        self.cookies.append(cookie)

    def save(self):
        with open(self.filename, 'w') as out:
            out.write('cookies')


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.api = types.SimpleNamespace(YTMusic=FakeClient, setup=lambda headers_raw: json.dumps({'cookie': headers_raw.split('\n')[0][8:]}))
        self.dlp = types.SimpleNamespace(cookies=types.SimpleNamespace(extract_cookies_from_browser=lambda _: [], YoutubeDLCookieJar=FakeJar))
        self.api_patch = patch.object(catalog, 'ytmusicapi', self.api)
        self.dlp_patch = patch.object(catalog, 'yt_dlp', self.dlp)
        self.api_patch.start()
        self.dlp_patch.start()
        self.addCleanup(self.api_patch.stop)
        self.addCleanup(self.dlp_patch.stop)
        self.worker = catalog.Worker(self.temp.name)

    def test_fixture_normalizers(self):
        data = json.loads((Path(__file__).parent / 'fixtures/results.json').read_text())
        self.assertEqual(catalog.mapped(data['songs'], catalog.track), [{
            'videoId': 'one', 'title': 'Song', 'artists': ['Artist'], 'album': 'Album',
            'albumId': 'MPRE1', 'durationMs': 185000, 'thumb': 'large'}])
        self.assertEqual(catalog.mapped(data['items'], catalog.item), [
            {'kind': 'song', 'id': 'one', 'title': 'Song', 'subtitle': 'Artist', 'thumb': ''},
            {'kind': 'album', 'id': 'MPRE1', 'title': 'Album', 'subtitle': '', 'thumb': ''},
            {'kind': 'playlist', 'id': 'PL1', 'title': 'Playlist', 'subtitle': '', 'thumb': ''},
            {'kind': 'artist', 'id': 'UC1', 'title': 'Artist', 'subtitle': '', 'thumb': ''}])
        self.assertEqual(len(catalog.mapped(data['songs'] * 250, catalog.track)), 200)
        self.assertEqual(len(catalog.text('x' * 400)), 300)

    def test_lyrics(self):
        self.assertEqual(catalog.parse_lyrics({'syncedLyrics': '[00:05.50][00:01.2] hi\n[00:03.00]there'}),
                         {'state': 'synced', 'lines': [{'t': 1200, 'text': 'hi'}, {'t': 3000, 'text': 'there'}, {'t': 5500, 'text': 'hi'}]})
        self.assertEqual(catalog.parse_lyrics({'plainLyrics': 'one\ntwo'}), {'state': 'plain', 'lines': [{'t': -1, 'text': 'one'}, {'t': -1, 'text': 'two'}]})
        self.assertEqual(catalog.parse_lyrics({})['state'], 'missing')

    def test_no_session_writes_nothing(self):
        with self.assertRaises(catalog.WorkerError) as exc:
            self.worker.sign_in('chromium')
        self.assertEqual(exc.exception.code, 'no_session')
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])

    def test_sign_in_writes_private_files(self):
        cookie = types.SimpleNamespace(domain='.youtube.com', name='__Secure-3PAPISID', value='secret')
        self.dlp.cookies.extract_cookies_from_browser = lambda _: [cookie]
        self.assertEqual(self.worker.sign_in('chromium'), {'signedIn': True})
        auth = Path(self.temp.name) / 'auth.json'
        self.assertIn('cookie', json.loads(auth.read_text()))
        self.assertEqual(auth.stat().st_mode & 0o777, 0o600)
        self.assertEqual((Path(self.temp.name) / 'cookies.txt').stat().st_mode & 0o777, 0o600)

    def test_protocol_unknown_and_anonymous(self):
        inp = io.StringIO('{"id":1,"method":"bogus","params":{}}\n{"id":2,"method":"likedSongs"}\n')
        out = io.StringIO()
        catalog.serve(inp, out, worker=self.worker)
        messages = [json.loads(line) for line in out.getvalue().splitlines()]
        self.assertEqual(messages[0], {'id': 0, 'result': {'ready': True, 'signedIn': False}})
        replies = {m['id']: m for m in messages[1:]}
        self.assertEqual(replies[1]['error']['code'], 'bad_request')
        self.assertEqual(replies[2]['error']['code'], 'not_signed_in')

    def test_search_and_artist_shapes(self):
        self.worker.client.search = lambda *a, **k: [
            {'category': 'Top result', 'resultType': 'video', 'videoId': 'v', 'title': 'Video'},
            {'resultType': 'single', 'browseId': 'MPRE1', 'title': 'Single'},
            {'resultType': 'song', 'title': 'missing'}]
        result = self.worker.call('search', {'query': 'x'})
        self.assertEqual(result['top']['id'], 'v')
        self.assertEqual([x['videoId'] for x in result['songs']], ['v'])
        self.assertEqual(result['albums'][0]['id'], 'MPRE1')
        self.worker.client.get_artist = lambda _: {'name': 'Artist', 'songs': {'results': [{'videoId': 'v'}]}, 'albums': {'results': [{'browseId': 'MPRE1', 'title': 'A'}]}}
        result = self.worker.call('artist', {'id': 'UC1'})
        self.assertEqual(result['tracks'][0]['videoId'], 'v')
        self.assertEqual(result['albums'][0]['id'], 'MPRE1')

        omv = {'videoId': 'b', 'title': 'Bahaya (Official Music Video)', 'artists': [{'name': 'Arsy Widianto'}], 'videoType': 'MUSIC_VIDEO_TYPE_OMV'}
        result = catalog.track(omv)
        self.assertEqual((result['title'], result['artists']), ('Bahaya', ['Arsy Widianto']))
        omv = dict(omv, title='Rimar - Tubin Tulat (Official Music Video)', artists=[{'name': 'Rimar Official'}])
        result = catalog.track(omv)
        self.assertEqual((result['title'], result['artists']), ('Tubin Tulat', ['Rimar']))
        self.assertEqual(catalog.track(dict(omv, title='Song - Part 2', artists=[{'name': 'A'}]))['title'], 'Song - Part 2')

if __name__ == '__main__':
    unittest.main()
