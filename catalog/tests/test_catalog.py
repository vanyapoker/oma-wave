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
        self.assertEqual([p.name for p in Path(self.temp.name).iterdir() if p.name != 'taste.sqlite3'], [])

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

    def test_video_titles_cleaned(self):
        ugc = {'videoId': 'x', 'title': 'КИНО | Пачка сигарет (Official Video)', 'artists': [{'name': 'mrPhakel'}], 'videoType': 'MUSIC_VIDEO_TYPE_UGC'}
        result = catalog.track(ugc)
        self.assertEqual((result['title'], result['artists']), ('Пачка сигарет', ['КИНО']))
        atv = {'videoId': 'y', 'title': 'Song - Part 2', 'artists': [{'name': 'A'}], 'videoType': 'MUSIC_VIDEO_TYPE_ATV'}
        self.assertEqual(catalog.track(atv)['title'], 'Song - Part 2')
        plain = dict(ugc, title='Around the World')
        self.assertEqual(catalog.track(plain)['artists'], ['mrPhakel'])
        self.assertEqual(catalog.track({'videoId': 'z', 'title': 't', 'length': '7:10'})['durationMs'], 430000)
        omv = {'videoId': 'b', 'title': 'Bahaya (Official Music Video)', 'artists': [{'name': 'Arsy Widianto'}], 'videoType': 'MUSIC_VIDEO_TYPE_OMV'}
        result = catalog.track(omv)
        self.assertEqual((result['title'], result['artists']), ('Bahaya', ['Arsy Widianto']))
        omv = dict(omv, title='Rimar - Tubin Tulat (Official Music Video)', artists=[{'name': 'Rimar Official'}])
        result = catalog.track(omv)
        self.assertEqual((result['title'], result['artists']), ('Tubin Tulat', ['Rimar']))
        self.assertEqual(catalog.track(dict(omv, title='Song - Part 2', artists=[{'name': 'A'}]))['title'], 'Song - Part 2')

    def test_classify_and_rate(self):
        self.assertEqual(catalog.classify(25000, 200000, False), 'skip')
        self.assertEqual(catalog.classify(150000, 200000, False), 'complete')
        self.assertEqual(catalog.classify(25000, 200000, True), 'partial')
        trk = {'videoId': 'a', 'title': 'T', 'artists': ['X']}
        self.assertEqual(self.worker.call('rate', {'track': trk, 'rating': 'like'}), {'rating': 'like', 'synced': False})
        self.assertEqual(self.worker.call('rating', {'videoId': 'a'}), {'rating': 'like'})
        self.worker.call('rate', {'track': trk, 'rating': 'none'})
        self.assertEqual(self.worker.call('rating', {'videoId': 'a'}), {'rating': 'none'})
        with self.assertRaises(catalog.WorkerError):
            self.worker.call('rate', {'track': trk, 'rating': 'bogus'})
        self.assertEqual(self.worker.call('logPlay', {'track': trk, 'listenedMs': 500, 'interrupted': True})['outcome'], 'ignored')

    def test_rate_syncs_only_when_signed_in(self):
        calls = []
        self.worker.client.rate_song = lambda video_id, status: calls.append((video_id, status.name))
        trk = {'videoId': 'a', 'title': 'T', 'artists': []}
        self.worker.call('rate', {'track': trk, 'rating': 'like'})
        self.assertEqual(calls, [])
        self.worker.signed_in = True
        self.assertTrue(self.worker.call('rate', {'track': trk, 'rating': 'dislike'})['synced'])
        self.assertEqual(calls, [('a', 'DISLIKE')])

    def wave_worker(self, tracks_by_call=None):
        def watch(videoId=None, playlistId=None, radio=False, limit=25):
            return {'tracks': [{'videoId': 'm%d' % i, 'title': 'M%d' % i, 'artists': [{'name': 'Art%d' % (i // 2)}], 'length': '3:00'} for i in range(8)]}
        client = types.SimpleNamespace(
            get_mood_categories=lambda: {'Moods & moments': [{'title': 'Chill', 'params': 'CHILLP'}, {'title': 'Sad', 'params': 'SADP'}]},
            get_mood_playlists=lambda params: self.mood_calls.append(params) or [{'title': 'p', 'playlistId': 'PL1'}],
            get_watch_playlist=watch, get_history=lambda: [], get_liked_songs=lambda limit: {'tracks': []})
        self.mood_calls = []
        self.worker.client = client
        self.worker.random = __import__('random').Random(1)
        return self.worker

    def test_wave_cold_start_and_filters(self):
        worker = self.wave_worker()
        worker.call('rate', {'track': {'videoId': 'm0', 'title': 'M0', 'artists': ['Art0']}, 'rating': 'dislike'})
        result = worker.call('waveNext', {'mood': 'calm', 'exclude': ['m5'], 'count': 10})['tracks']
        self.assertEqual(self.mood_calls, ['CHILLP'])
        ids = [t['videoId'] for t in result]
        self.assertTrue(0 < len(ids) <= 10)
        self.assertNotIn('m0', ids)
        self.assertNotIn('m5', ids)
        # Only 2 per artist: 8 tracks over 4 artists, one artist's tracks partly dropped.
        artists = [t['artists'][0] for t in result]
        self.assertTrue(all(artists.count(a) <= 2 for a in artists))

    def test_wave_diversity_and_discover(self):
        worker = self.wave_worker()
        worker.client.get_watch_playlist = lambda **k: {'tracks': [
            {'videoId': 'k%d' % i, 'title': 'K', 'artists': [{'name': 'Same'}]} for i in range(5)]
            + [{'videoId': 'n%d' % i, 'title': 'N', 'artists': [{'name': 'New%d' % i}]} for i in range(3)]}
        self.assertLessEqual(len([t for t in worker.call('waveNext', {})['tracks'] if t['artists'] == ['Same']]), 2)
        for i in range(5):
            worker.taste.log_play({'videoId': 'k%d' % i, 'title': 'K', 'artists': ['Same']}, 200000, 200000, 'other', False)
        result = worker.call('waveNext', {'character': 'discover', 'count': 3})['tracks']
        self.assertTrue(all(t['videoId'].startswith('n') for t in result))

    def test_wave_bad_input(self):
        with self.assertRaises(catalog.WorkerError) as exc:
            self.wave_worker().call('waveNext', {'mood': 'bogus'})
        self.assertEqual(exc.exception.code, 'bad_request')


if __name__ == '__main__':
    unittest.main()
