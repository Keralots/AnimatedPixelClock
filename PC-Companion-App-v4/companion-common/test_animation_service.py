"""Conversion limits, duration preservation, upload guards and HTTP integration."""
import base64
import io
import json
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from PIL import Image
import animation_service as service


def gif_blob(count=12, duration=100):
    frames = [Image.new('RGB', (128, 64), (i * 17 % 256, i * 31 % 256, 40)) for i in range(count)]
    out = io.BytesIO()
    frames[0].save(out, format='GIF', save_all=True, append_images=frames[1:], duration=duration, loop=0)
    return base64.b64encode(out.getvalue()).decode('ascii')


class ConversionTests(unittest.TestCase):
    def test_small_board_budget_preserves_duration(self):
        result = service.convert_gif({'gif': gif_blob(30), 'maxBytes': 98304})
        self.assertLessEqual(result['bytes'], 98304)
        self.assertEqual(result['frames'], 15)
        self.assertEqual(result['seconds'], 3)
        self.assertEqual(result['frameSkip'], 2)
        service.validate_pca(base64.b64decode(result['pca']))

    def test_no_auto_fit_reports_capacity(self):
        with self.assertRaisesRegex(ValueError, 'does not fit'):
            service.convert_gif({'gif': gif_blob(30), 'maxBytes': 98304, 'autoFit': False})

    def test_one_color_still_has_valid_two_color_header(self):
        result = service.convert_gif({'gif': gif_blob(1)})
        service.validate_pca(base64.b64decode(result['pca']))

    def test_fit_and_anchor_variants(self):
        for fit in ('crop', 'pad', 'stretch'):
            for anchor in ('start', 'center', 'end'):
                result = service.convert_gif({'gif': gif_blob(2), 'fit': fit, 'anchor': anchor})
                self.assertEqual(result['frames'], 2)
                with Image.open(io.BytesIO(base64.b64decode(result['preview']))) as preview:
                    self.assertEqual(preview.size, (512, 256))

    def test_invalid_options_and_data(self):
        for options in ({'gif':'!'}, {'gif':gif_blob(), 'colors':1}, {'gif':gif_blob(), 'frameSkip':0}, {'gif':gif_blob(), 'maxBytes':100}, {'gif':gif_blob(), 'fit':'bad'}):
            with self.assertRaises(ValueError): service.convert_gif(options)
        for name in ('../x', 'x y', 'x\r\n', 'ą', ''):
            with self.assertRaises(ValueError): service.animation_name(name)

    def test_upload_rechecks_space_before_sending(self):
        result = service.convert_gif({'gif': gif_blob(3)})
        with patch.object(service, 'device_json', return_value={'usable':True, 'maxUploadBytes':100}) as device:
            with self.assertRaisesRegex(ValueError, 'Storage changed'):
                service.upload_animation({'esp32_ip':'pixelclock.local'}, {'name':'clip','pca':result['pca']})
            self.assertEqual(device.call_count, 1)

    def test_truncated_payload_rejected(self):
        result = service.convert_gif({'gif':gif_blob(2)})
        with self.assertRaises(ValueError): service.validate_pca(base64.b64decode(result['pca'])[:-1])


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import server
        state = SimpleNamespace(get_config=lambda: {'esp32_ip':'pixelclock.local'})
        cls.httpd, cls.port = server.make_server(SimpleNamespace(), state, port=0)
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown(); cls.httpd.server_close(); cls.thread.join()

    def test_convert_route_and_static_asset(self):
        base = 'http://127.0.0.1:' + str(self.port)
        with urlopen(base+'/animations.js') as response:
            self.assertIn(b'gifConvert', response.read())
        request = Request(base+'/api/animations/convert', data=json.dumps({'gif':gif_blob(3)}).encode(), headers={'Content-Type':'application/json'})
        with urlopen(request) as response:
            self.assertEqual(json.load(response)['frames'], 3)

    def test_cross_origin_rejected(self):
        request = Request('http://127.0.0.1:'+str(self.port)+'/api/animations/convert', data=b'{}', headers={'Content-Type':'application/json','Origin':'https://example.com'})
        with self.assertRaises(HTTPError) as error: urlopen(request)
        self.assertEqual(error.exception.code, 403)


if __name__ == '__main__': unittest.main()
