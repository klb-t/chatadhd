"""Procedural, antialiased 2D face fixture; not an anatomical or affect model.

Only Python stdlib + NumPy. No fonts, external assets, network or model calls.
Coordinates belong to this stylized drawing, not calibrated FACS units.
"""
import math
import struct
import zlib
import numpy as np

VERSION = 'procedural-2d-v1'
EXPRESSION_DEFAULTS = dict(brow_raise_l=0., brow_raise_r=0., brow_slant_l=0.,
                         brow_slant_r=0., eye_open_l=.65, eye_open_r=.65,
                         mouth_corner_l=0., mouth_corner_r=0., jaw_open=.12)
IDENTITY_DEFAULTS = dict(head_width=1., head_height=1., eye_spacing=1., nose_width=1.)
POSE_DEFAULTS = dict(yaw_approx_deg=0., roll_deg=0.)
LIGHT_DEFAULTS = dict(horizontal=0., intensity=1.)


def config():
    return dict(expression=EXPRESSION_DEFAULTS.copy(), identity=IDENTITY_DEFAULTS.copy(),
                pose=POSE_DEFAULTS.copy(), light=LIGHT_DEFAULTS.copy(), blank=False,
                renderer=VERSION, size=256, supersampling=2, seed=20261101)


def validate(c):
    assert c['renderer'] == VERSION
    assert c['size'] == 256 and c['supersampling'] == 2
    assert set(c['expression']) == set(EXPRESSION_DEFAULTS)
    assert set(c['identity']) == set(IDENTITY_DEFAULTS)
    assert set(c['pose']) == set(POSE_DEFAULTS)
    assert set(c['light']) == set(LIGHT_DEFAULTS)
    for k, v in c['expression'].items():
        assert math.isfinite(v)
        assert (0 <= v <= 1) if ('eye_open' in k or k == 'jaw_open') else (-1 <= v <= 1)
    for v in c['identity'].values():
        assert .75 <= v <= 1.25
    assert abs(c['pose']['yaw_approx_deg']) <= 20
    assert abs(c['pose']['roll_deg']) <= 12
    assert abs(c['light']['horizontal']) <= 1
    assert .8 <= c['light']['intensity'] <= 1.2


def png_bytes(rgb):
    rgb = np.ascontiguousarray(rgb, dtype=np.uint8)
    h, w, channels = rgb.shape
    assert channels == 3
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + row.tobytes() for row in rgb)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def render(c):
    validate(c)
    n = c['size'] * c['supersampling']
    image = np.empty((n, n, 3), dtype=np.float64)
    image[:] = [232, 236, 240]
    if c['blank']:
        return image.reshape(256, 2, 256, 2, 3).mean(axis=(1, 3)).astype(np.uint8)
    # Inverse affine transformation for screen-space roll and approximate yaw.
    # Yaw is a 2D nuisance warp, NOT a physical 3D camera/viewpoint.
    y, x = np.mgrid[0:n, 0:n].astype(float)
    x = (x + .5) / n - .5
    y = (y + .5) / n - .5
    roll = math.radians(c['pose']['roll_deg'])
    u = x * math.cos(roll) + y * math.sin(roll)
    v = -x * math.sin(roll) + y * math.cos(roll)
    yaw = math.radians(c['pose']['yaw_approx_deg'])
    u = u / math.cos(yaw)
    ident, e, light = c['identity'], c['expression'], c['light']
    u /= ident['head_width']
    v /= ident['head_height']

    def ellipse(cx, cy, rx, ry, color):
        mask = ((u - cx) / rx) ** 2 + ((v - cy) / ry) ** 2 <= 1
        image[mask] = color
        return mask

    def polyline(points, width, color):
        # Bound each segment's work; canonical coordinates already include pose.
        mask = np.zeros((n, n), dtype=bool)
        for a, b in zip(points[:-1], points[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            t = np.clip(((u - a[0]) * dx + (v - a[1]) * dy) / max(dx*dx + dy*dy, 1e-16), 0, 1)
            mask |= (u - a[0] - t*dx)**2 + (v - a[1] - t*dy)**2 <= width**2
        image[mask] = color

    def bezier(a, b, d, width, color):
        t = np.linspace(0, 1, 18)[:, None]
        points = (1-t)**2 * np.array(a) + 2*(1-t)*t*np.array(b) + t*t*np.array(d)
        polyline(points, width, color)

    ellipse(-.285, -.025, .04, .085, [184, 133, 103])
    ellipse(.285, -.025, .04, .085, [184, 133, 103])
    ellipse(0, .34, .115, .15, [182, 134, 105])
    head = ellipse(0, -.015, .285, .38, [205, 161, 126])
    # Identical shader for all expression samples; nuisance light has own block.
    shade = (.93 + .075*np.sqrt(np.clip(1-(u/.285)**2-(v/.38)**2, 0, 1))
             + .15*light['horizontal']*u/.285) * light['intensity']
    image[head] *= shade[head, None]
    ink = [62, 44, 38]
    for side, sign in [('l', -1), ('r', 1)]:
        cx = sign * .112 * ident['eye_spacing']
        open_y = .006 + .024*e['eye_open_'+side]
        ellipse(cx, -.085, .061, open_y, [246, 244, 231])
        ellipse(cx, -.085, .017, min(.019, open_y), [75, 80, 57])
        ellipse(cx, -.085, .008, min(.012, open_y), [28, 29, 28])
        # Local upper eyelid follows eye opening, without text/semantic labels.
        bezier((cx-.06, -.085), (cx, -.085-2*open_y), (cx+.06, -.085), .0035, ink)
        raised = -.168 - .032*e['brow_raise_'+side]
        slope = .028*e['brow_slant_'+side]
        bezier((cx-.064, raised-sign*slope), (cx, raised-.012),
               (cx+.064, raised+sign*slope), .006, ink)
    nose = .026*ident['nose_width']
    # Offset nose with yaw only as a visible controllable 2D warp control.
    nose_shift = .04*math.sin(yaw)
    bezier((nose_shift, -.065), (nose_shift+.012, .025), (nose_shift+nose, .05), .003, [152, 106, 82])
    bezier((nose_shift-nose, .05), (nose_shift, .067), (nose_shift+nose, .05), .003, [152, 106, 82])
    left_y = .174 - .043*e['mouth_corner_l']
    right_y = .174 - .043*e['mouth_corner_r']
    gap = .055*e['jaw_open']
    # Mouth as a closed quadratic lens, rasterized in canonical coordinates.
    t = np.clip((u+.102)/.204, 0, 1)
    mid_y = (1-t)**2*left_y + 2*(1-t)*t*.18 + t*t*right_y
    half_gap = gap * 4*t*(1-t)
    inside = (abs(u) <= .102) & (abs(v-mid_y) <= half_gap)
    image[inside] = [68, 28, 30]
    bezier((-.102, left_y), (0, .18-2*gap), (.102, right_y), .004, [136, 65, 62])
    bezier((-.102, left_y), (0, .18+2*gap), (.102, right_y), .004, [136, 65, 62])
    result = np.clip(image, 0, 255).reshape(256, 2, 256, 2, 3).mean(axis=(1, 3))
    return np.rint(result).astype(np.uint8)
