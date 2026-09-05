"""Bounded GIF conversion and animation transfer for the desktop companion."""
import base64
import io
import json
import math
import re
import struct
from urllib.parse import quote
from urllib.request import Request, urlopen
from urllib.error import HTTPError

MAX_INPUT_BYTES = 8 * 1024 * 1024
MAX_PCA_BYTES = 1536 * 1024


def device_url(config):
    host = str(config.get('esp32_ip', '')).strip()
    if not host or not re.fullmatch(r'[a-zA-Z0-9.-]+(?::[0-9]{1,5})?', host):
        raise ValueError('Set and save the device address on the Connection page first.')
    return 'http://' + host


def device_json(config, path, data=None, headers=None):
    request = Request(device_url(config) + path, data=data, headers=headers or {})
    try:
        with urlopen(request, timeout=30 if data else 5) as response:
            result = json.loads(response.read(256 * 1024))
    except HTTPError as error:
        detail = error.read(4096).decode('utf-8', 'replace')
        raise ValueError('Device rejected the request: ' + detail) from error
    if result.get('error'):
        raise ValueError(result['error'])
    return result


def decode_blob(encoded, maximum):
    if not isinstance(encoded, str) or len(encoded) > ((maximum + 2) // 3) * 4:
        raise ValueError('File exceeds the size limit.')
    try:
        data = base64.b64decode(encoded, validate=True)
    except (ValueError, TypeError) as error:
        raise ValueError('Invalid file encoding.') from error
    if not data or len(data) > maximum:
        raise ValueError('Empty or oversized file.')
    return data


def convert_gif(options):
    try:
        from PIL import Image
        import gif_converter as codec
    except ImportError as error:
        raise ValueError('GIF conversion needs Pillow. Install the companion requirements.') from error
    fit, anchor = options.get('fit', 'crop'), options.get('anchor', 'center')
    if fit not in ('crop', 'pad', 'stretch') or anchor not in ('start', 'center', 'end'):
        raise ValueError('Invalid fit or crop anchor.')
    colors = int(options.get('colors', 16))
    skip = int(options.get('frameSkip', 1))
    budget = min(MAX_PCA_BYTES, int(options.get('maxBytes', MAX_PCA_BYTES)))
    if not 2 <= colors <= 16 or not 1 <= skip <= 1000 or budget < 4142:
        raise ValueError('Check palette size, frame skip and available device storage.')
    raw = decode_blob(options.get('gif'), MAX_INPUT_BYTES)
    frames, delays = [], []
    with Image.open(io.BytesIO(raw)) as source:
        if source.format != 'GIF':
            raise ValueError('Choose a GIF file.')
        if source.width * source.height > 4_000_000:
            raise ValueError('GIF dimensions exceed 4 megapixels. Resize it first.')
        pixels = 0
        for index in range(1001):
            try:
                source.seek(index)
            except EOFError:
                break
            pixels += source.width * source.height
            if index == 1000 or pixels > 100_000_000:
                raise ValueError('GIF is too long or large. Trim or resize it first.')
            frames.append(codec.fit_frame(source.convert('RGB'), fit, anchor))
            delays.append(max(34, min(5000, int(source.info.get('duration', 100)) or 100)))
    max_frames = min(360, (budget - 44) // 4098)
    if options.get('autoFit', True):
        skip = max(skip, math.ceil(len(frames) / max_frames))
    selected = frames[::skip]
    merged = [sum(delays[i:i + skip]) for i in range(0, len(delays), skip)]
    if len(selected) > max_frames:
        raise ValueError('Animation does not fit. Enable fit-to-storage or increase frame skip.')
    if any(d > 5000 for d in merged):
        raise ValueError('Too little storage to preserve timing. Trim the GIF into a shorter clip.')
    quantized, palette = codec.quantize_frames(selected, colors)
    blob = codec.build_pca(quantized, palette, merged)
    if len(blob) > budget:
        raise ValueError('Converted animation exceeds the upload budget.')
    preview = io.BytesIO()
    images = [f.convert('RGB').resize((512, 256), Image.Resampling.NEAREST) for f in quantized]
    images[0].save(preview, format='GIF', save_all=True, append_images=images[1:], duration=merged, loop=0)
    return {'pca': base64.b64encode(blob).decode('ascii'),
            'preview': base64.b64encode(preview.getvalue()).decode('ascii'),
            'frames': len(selected), 'sourceFrames': len(frames), 'bytes': len(blob),
            'seconds': sum(merged) / 1000, 'frameSkip': skip}


def animation_name(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,24}', value):
        raise ValueError('Use a name of 1-24 letters, digits, underscores or hyphens.')
    return value


def validate_pca(blob):
    if len(blob) < 12 or blob[:4] != b'PCA1':
        raise ValueError('Invalid converted animation.')
    frames, delay, palette, flags, reserved = struct.unpack('<HHBBH', blob[4:12])
    if not 1 <= frames <= 360 or not 20 <= delay <= 5000 or not 2 <= palette <= 16:
        raise ValueError('Invalid animation header.')
    if len(blob) != 12 + palette * 2 + frames * 4098:
        raise ValueError('Invalid animation length.')


def upload_animation(config, options):
    name = animation_name(options.get('name'))
    blob = decode_blob(options.get('pca'), MAX_PCA_BYTES)
    validate_pca(blob)
    status = device_json(config, '/api/anim/list')
    budget = status.get('maxUploadBytes', max(0, status.get('free', 0) - 16384))
    if not status.get('usable'):
        raise ValueError('Animation storage is unavailable. Update the device firmware first.')
    if len(blob) > budget:
        raise ValueError('Storage changed or the file is too large. Refresh storage and preview again.')
    boundary = '----PixelClockAnimation'
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="anim"; filename="{name}.pca"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n').encode() + blob + f'\r\n--{boundary}--\r\n'.encode()
    return device_json(config, '/api/anim/upload?name=' + quote(name), body,
                       {'Content-Type': 'multipart/form-data; boundary=' + boundary})
