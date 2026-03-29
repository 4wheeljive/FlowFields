import colorsys
import math
import sys
from dataclasses import dataclass

import pygame

GRID_SIZE = 32
CELL_SIZE = 16
GRID_PIXELS = GRID_SIZE * CELL_SIZE
RIGHT_UI_WIDTH = 330
MARGIN = 20
FPS = 60

COLOR_SHIFT_DEFAULT = 0.10
ENDPOINT_SPEED_DEFAULT = 0.35
SPIRAL_TRANSPORT_FRACTION = 0.45
SPIRAL_DIM_DEFAULT = 1.00
ROUND_SPIRAL_ANGULAR_STEP = 0.28
ROUND_SPIRAL_RADIAL_STEP = 0.18
ORBIT_DOT_DIAMETER = 2.5
ORBIT_RADIUS = 9.5

WINDOW_WIDTH = MARGIN * 3 + GRID_PIXELS + RIGHT_UI_WIDTH
WINDOW_HEIGHT = MARGIN * 2 + GRID_PIXELS

BG = (15, 18, 24)
TEXT = (230, 230, 240)
TRACK = (50, 56, 70)
FILL = (70, 130, 255)
KNOB = (235, 239, 248)
PANEL = (22, 26, 36)
BORDER = (70, 70, 85)

MODE_NAMES = [
    "No Trail",
    "2. Chromatic Flow Split",
    "4. Polar Warp Pulsation",
    "4b. Polar Warp 2",
    "6. Shockwave Displacement",
    "6b. Shockwave 2",
    "7. Rotating Coordinate",
    "7b. Rotating Wind",
    "7c. Rotating Wind Wave",
    "Meandering Jet Field",
    "10. Attractor Fields",
    "15. Fractal Spiral Step",
    "Ring Flow",
    "Ring Flow 2",
    "Square Spiral Stream",
    "Spiral Stream",
    "Spiral Outwards",
    "To the center",
    "From the center",
    "Directional Noise",
    "Wind + Noise Alpha Mask",
    "Rings",
]

MODE_BUTTON_LABELS = [
    "No Trail",
    "Chrom Split",
    "Polar Warp",
    "Polar Warp 2",
    "Shockwave",
    "Shockwave 2",
    "Rotate Coord",
    "Rotating Wind",
    "Rotating Wind Wave",
    "Meandering Jet",
    "Attractors",
    "Fractal Spiral",
    "Ring Flow",
    "Ring Flow 2",
    "Square Spiral",
    "Spiral In",
    "Spiral Out",
    "To Center",
    "From Center",
    "Directional Noise",
    "Wind + Noise Alpha Mask",
    "Rings",
]


@dataclass
class Slider:
    label: str
    min_value: float
    max_value: float
    value: float
    rect: pygame.Rect
    decimals: int = 2
    dragging: bool = False

    def _set_from_x(self, x: int) -> None:
        t = (x - self.rect.left) / self.rect.width
        t = max(0.0, min(1.0, t))
        self.value = self.min_value + t * (self.max_value - self.min_value)

    def handle_event(self, event: pygame.event.Event) -> None:
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and self.rect.collidepoint(event.pos):
            self.dragging = True
            self._set_from_x(event.pos[0])
        elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            self.dragging = False
        elif event.type == pygame.MOUSEMOTION and self.dragging:
            self._set_from_x(event.pos[0])

    def draw(self, surface: pygame.Surface, font: pygame.font.Font) -> None:
        pygame.draw.rect(surface, TRACK, self.rect, border_radius=5)
        t = (self.value - self.min_value) / (self.max_value - self.min_value)
        fill_width = int(self.rect.width * t)
        if fill_width > 0:
            pygame.draw.rect(surface, FILL, (self.rect.left, self.rect.top, fill_width, self.rect.height), border_radius=5)
        knob_x = self.rect.left + fill_width
        pygame.draw.circle(surface, KNOB, (knob_x, self.rect.centery), self.rect.height // 2 + 3)
        label = f"{self.label}: {self.value:.{self.decimals}f}"
        surface.blit(font.render(label, True, TEXT), (self.rect.left, self.rect.top - 20))


@dataclass
class Button:
    label: str
    rect: pygame.Rect
    active: bool = False

    def handle_event(self, event: pygame.event.Event) -> bool:
        return event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and self.rect.collidepoint(event.pos)

    def draw(self, surface: pygame.Surface, font: pygame.font.Font) -> None:
        fill = (74, 124, 189) if self.active else (52, 52, 62)
        pygame.draw.rect(surface, fill, self.rect, border_radius=6)
        pygame.draw.rect(surface, BORDER, self.rect, 1, border_radius=6)
        text = font.render(self.label, True, TEXT)
        surface.blit(text, (self.rect.centerx - text.get_width() // 2, self.rect.centery - text.get_height() // 2))


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def hsv_color_with_phase(t: float, speed: float, phase: float) -> tuple[int, int, int]:
    hue = (t * speed + phase) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


def get_rgb(surface: pygame.Surface, x: int, y: int) -> tuple[int, int, int]:
    if 0 <= x < GRID_SIZE and 0 <= y < GRID_SIZE:
        c = surface.get_at((x, y))
        return c.r, c.g, c.b
    return 0, 0, 0


def set_rgb(surface: pygame.Surface, x: int, y: int, color: tuple[int, int, int]) -> None:
    if 0 <= x < GRID_SIZE and 0 <= y < GRID_SIZE:
        surface.set_at((x, y), color)


def sample_rgb_bilinear(surface: pygame.Surface, x: float, y: float) -> tuple[float, float, float]:
    x = clamp(x, 0.0, (GRID_SIZE - 1) - 1e-6)
    y = clamp(y, 0.0, (GRID_SIZE - 1) - 1e-6)

    x0 = int(math.floor(x))
    y0 = int(math.floor(y))
    x1 = min(GRID_SIZE - 1, x0 + 1)
    y1 = min(GRID_SIZE - 1, y0 + 1)
    fx = x - x0
    fy = y - y0

    c00 = get_rgb(surface, x0, y0)
    c10 = get_rgb(surface, x1, y0)
    c01 = get_rgb(surface, x0, y1)
    c11 = get_rgb(surface, x1, y1)

    r0 = c00[0] * (1.0 - fx) + c10[0] * fx
    g0 = c00[1] * (1.0 - fx) + c10[1] * fx
    b0 = c00[2] * (1.0 - fx) + c10[2] * fx

    r1 = c01[0] * (1.0 - fx) + c11[0] * fx
    g1 = c01[1] * (1.0 - fx) + c11[1] * fx
    b1 = c01[2] * (1.0 - fx) + c11[2] * fx

    return (
        r0 * (1.0 - fy) + r1 * fy,
        g0 * (1.0 - fy) + g1 * fy,
        b0 * (1.0 - fy) + b1 * fy,
    )


def transport_and_dim(
    surface: pygame.Surface,
    x: int,
    y: int,
    sx: int,
    sy: int,
    fraction: float,
    dim: float,
) -> None:
    c0 = get_rgb(surface, x, y)
    c1 = get_rgb(surface, sx, sy)
    r = c0[0] * (1.0 - fraction) + c1[0] * fraction
    g = c0[1] * (1.0 - fraction) + c1[1] * fraction
    b = c0[2] * (1.0 - fraction) + c1[2] * fraction
    set_rgb(surface, x, y, (int(round(r * dim)), int(round(g * dim)), int(round(b * dim))))


def apply_square_spiral_tail(
    surface: pygame.Surface, cx: int, cy: int, radius: int, fraction: float, dim: float
) -> None:
    for d in range(radius, -1, -1):
        for i in range(cx - d, cx + d + 1):
            transport_and_dim(surface, i, cy - d, i + 1, cy - d, fraction, dim)
        for i in range(cy - d, cy + d + 1):
            transport_and_dim(surface, cx + d, i, cx + d, i + 1, fraction, dim)
        for i in range(cx + d, cx - d - 1, -1):
            transport_and_dim(surface, i, cy + d, i - 1, cy + d, fraction, dim)
        for i in range(cy + d, cy - d - 1, -1):
            transport_and_dim(surface, cx - d, i, cx - d, i - 1, fraction, dim)


def apply_round_spiral_tail(
    surface: pygame.Surface,
    cx: float,
    cy: float,
    radius: float,
    fraction: float,
    dim: float,
    outward: bool = False,
) -> None:
    src = surface.copy()
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy)
            if r > radius:
                c = get_rgb(src, x, y)
                set_rgb(surface, x, y, (int(round(c[0] * dim)), int(round(c[1] * dim)), int(round(c[2] * dim))))
                continue

            theta = math.atan2(dy, dx)
            if outward:
                sample_r = max(0.0, r - ROUND_SPIRAL_RADIAL_STEP)
            else:
                sample_r = min(radius + 1.5, r + ROUND_SPIRAL_RADIAL_STEP)
            sample_theta = theta - ROUND_SPIRAL_ANGULAR_STEP
            sx = cx + math.cos(sample_theta) * sample_r
            sy = cy + math.sin(sample_theta) * sample_r

            c0 = get_rgb(src, x, y)
            c1 = sample_rgb_bilinear(src, sx, sy)
            nr = (c0[0] * (1.0 - fraction) + c1[0] * fraction) * dim
            ng = (c0[1] * (1.0 - fraction) + c1[1] * fraction) * dim
            nb = (c0[2] * (1.0 - fraction) + c1[2] * fraction) * dim
            set_rgb(surface, x, y, (int(round(nr)), int(round(ng)), int(round(nb))))


def apply_to_center_tail(surface: pygame.Surface, cx: float, cy: float, fraction: float, dim: float) -> None:
    src = surface.copy()
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy)
            if r > 1e-6:
                ux = dx / r
                uy = dy / r
                sx = x + ux * ROUND_SPIRAL_RADIAL_STEP
                sy = y + uy * ROUND_SPIRAL_RADIAL_STEP
                c1 = sample_rgb_bilinear(src, sx, sy)
            else:
                c1 = get_rgb(src, x, y)
            c0 = get_rgb(src, x, y)
            nr = (c0[0] * (1.0 - fraction) + c1[0] * fraction) * dim
            ng = (c0[1] * (1.0 - fraction) + c1[1] * fraction) * dim
            nb = (c0[2] * (1.0 - fraction) + c1[2] * fraction) * dim
            set_rgb(surface, x, y, (int(round(nr)), int(round(ng)), int(round(nb))))


def apply_from_center_tail(surface: pygame.Surface, cx: float, cy: float, fraction: float, dim: float) -> None:
    src = surface.copy()
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy)
            if r > 1e-6:
                ux = dx / r
                uy = dy / r
                sx = x - ux * ROUND_SPIRAL_RADIAL_STEP
                sy = y - uy * ROUND_SPIRAL_RADIAL_STEP
                c1 = sample_rgb_bilinear(src, sx, sy)
            else:
                c1 = get_rgb(src, x, y)
            c0 = get_rgb(src, x, y)
            nr = (c0[0] * (1.0 - fraction) + c1[0] * fraction) * dim
            ng = (c0[1] * (1.0 - fraction) + c1[1] * fraction) * dim
            nb = (c0[2] * (1.0 - fraction) + c1[2] * fraction) * dim
            set_rgb(surface, x, y, (int(round(nr)), int(round(ng)), int(round(nb))))


def blend_pixel_weighted(surface: pygame.Surface, px: int, py: int, color: tuple[int, int, int], w: float) -> None:
    if not (0 <= px < GRID_SIZE and 0 <= py < GRID_SIZE):
        return
    w = clamp(w, 0.0, 1.0)
    if w <= 0.0:
        return
    old = surface.get_at((px, py))
    nr = int(old.r * (1.0 - w) + color[0] * w)
    ng = int(old.g * (1.0 - w) + color[1] * w)
    nb = int(old.b * (1.0 - w) + color[2] * w)
    surface.set_at((px, py), (nr, ng, nb))


def draw_aa_endpoint_disc(surface: pygame.Surface, cx: float, cy: float, color: tuple[int, int, int], radius: float = 0.85) -> None:
    min_x = max(0, int(math.floor(cx - radius - 1.0)))
    max_x = min(GRID_SIZE - 1, int(math.ceil(cx + radius + 1.0)))
    min_y = max(0, int(math.floor(cy - radius - 1.0)))
    max_y = min(GRID_SIZE - 1, int(math.ceil(cy + radius + 1.0)))
    for py in range(min_y, max_y + 1):
        for px in range(min_x, max_x + 1):
            dx = (px + 0.5) - cx
            dy = (py + 0.5) - cy
            dist = math.hypot(dx, dy)
            w = clamp(radius + 0.5 - dist, 0.0, 1.0)
            blend_pixel_weighted(surface, px, py, color, w)


def draw_aa_subpixel_ring(
    surface: pygame.Surface,
    cx: float,
    cy: float,
    color: tuple[int, int, int],
    radius: float = 2.5,
    thickness: float = 1.8,
) -> None:
    half_t = thickness * 0.5
    inner = max(0.0, radius - half_t)
    outer = radius + half_t
    min_x = max(0, int(math.floor(cx - outer - 1.0)))
    max_x = min(GRID_SIZE - 1, int(math.ceil(cx + outer + 1.0)))
    min_y = max(0, int(math.floor(cy - outer - 1.0)))
    max_y = min(GRID_SIZE - 1, int(math.ceil(cy + outer + 1.0)))
    for py in range(min_y, max_y + 1):
        for px in range(min_x, max_x + 1):
            dx = (px + 0.5) - cx
            dy = (py + 0.5) - cy
            dist = math.hypot(dx, dy)
            # Antialiased annulus coverage with ~1px smooth boundary.
            in_w = clamp(dist - inner + 0.5, 0.0, 1.0)
            out_w = clamp(outer - dist + 0.5, 0.0, 1.0)
            w = in_w * out_w
            blend_pixel_weighted(surface, px, py, color, w)


def draw_aa_subpixel_rainbow_ring(
    surface: pygame.Surface,
    cx: float,
    cy: float,
    t: float,
    color_shift: float,
    phase_dir: float = 1.0,
    radius: float = 5.625,
    thickness: float = 1.8,
) -> None:
    half_t = thickness * 0.5
    inner = max(0.0, radius - half_t)
    outer = radius + half_t
    min_x = max(0, int(math.floor(cx - outer - 1.0)))
    max_x = min(GRID_SIZE - 1, int(math.ceil(cx + outer + 1.0)))
    min_y = max(0, int(math.floor(cy - outer - 1.0)))
    max_y = min(GRID_SIZE - 1, int(math.ceil(cy + outer + 1.0)))
    for py in range(min_y, max_y + 1):
        for px in range(min_x, max_x + 1):
            dx = (px + 0.5) - cx
            dy = (py + 0.5) - cy
            dist = math.hypot(dx, dy)
            in_w = clamp(dist - inner + 0.5, 0.0, 1.0)
            out_w = clamp(outer - dist + 0.5, 0.0, 1.0)
            w = in_w * out_w
            if w <= 0.0:
                continue
            angle_norm = (math.atan2(dy, dx) / (2.0 * math.pi)) % 1.0
            color = hsv_color_with_phase(t, color_shift * phase_dir, angle_norm)
            blend_pixel_weighted(surface, px, py, color, w)


def draw_aa_subpixel_line(
    surface: pygame.Surface,
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    t: float,
    color_shift: float,
    phase_start: float = 0.0,
    phase_span: float = 1.0,
) -> None:
    dx = x1 - x0
    dy = y1 - y0
    steps = max(1, int(max(abs(dx), abs(dy)) * 3))
    for i in range(steps + 1):
        u = i / steps
        x = x0 + dx * u
        y = y0 + dy * u
        xi = math.floor(x)
        yi = math.floor(y)
        fx = x - xi
        fy = y - yi
        color = hsv_color_with_phase(t, color_shift, phase_start + u * phase_span)
        blend_pixel_weighted(surface, int(xi), int(yi), color, (1.0 - fx) * (1.0 - fy))
        blend_pixel_weighted(surface, int(xi + 1), int(yi), color, fx * (1.0 - fy))
        blend_pixel_weighted(surface, int(xi), int(yi + 1), color, (1.0 - fx) * fy)
        blend_pixel_weighted(surface, int(xi + 1), int(yi + 1), color, fx * fy)


def inject_lissajous_line(surface: pygame.Surface, t: float, color_shift: float, endpoint_speed: float) -> None:
    c = (GRID_SIZE - 1) * 0.5
    s = endpoint_speed
    x1 = c + 11.5 * math.sin(t * s * 1.13 + 0.20)
    y1 = c + 10.5 * math.sin(t * s * 1.71 + 1.30)
    x2 = c + 12.0 * math.sin(t * s * 1.89 + 2.20)
    y2 = c + 11.0 * math.sin(t * s * 1.37 + 0.70)
    draw_aa_subpixel_line(surface, x1, y1, x2, y2, t, color_shift)


def inject_orbiting_dots(surface: pygame.Surface, t: float, orbit_speed: float, color_shift: float) -> None:
    cx = (GRID_SIZE - 1) * 0.5
    cy = (GRID_SIZE - 1) * 0.5
    radius = ORBIT_DOT_DIAMETER * 0.5
    base_angle = t * orbit_speed
    for i in range(3):
        a = base_angle + i * (2.0 * math.pi / 3.0)
        x = cx + math.cos(a) * ORBIT_RADIUS
        y = cy + math.sin(a) * ORBIT_RADIUS
        color = hsv_color_with_phase(t, color_shift, i / 3.0)
        draw_aa_endpoint_disc(surface, x, y, color, radius=radius)


def inject_lissajous_triangle(surface: pygame.Surface, t: float, color_shift: float, endpoint_speed: float) -> None:
    c = (GRID_SIZE - 1) * 0.5
    s = endpoint_speed
    p1 = (
        c + 11.8 * math.sin(t * s * 1.13 + 0.20),
        c + 10.4 * math.sin(t * s * 1.71 + 1.30),
    )
    p2 = (
        c + 11.2 * math.sin(t * s * 1.89 + 2.20),
        c + 10.9 * math.sin(t * s * 1.37 + 0.70),
    )
    p3 = (
        c + 10.9 * math.sin(t * s * 1.57 + 3.10),
        c + 11.3 * math.sin(t * s * 1.21 + 2.40),
    )

    l12 = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
    l23 = math.hypot(p3[0] - p2[0], p3[1] - p2[1])
    l31 = math.hypot(p1[0] - p3[0], p1[1] - p3[1])
    total = max(1e-6, l12 + l23 + l31)

    s1 = 0.0
    s2 = l12 / total
    s3 = (l12 + l23) / total

    draw_aa_subpixel_line(surface, p1[0], p1[1], p2[0], p2[1], t, color_shift, phase_start=s1, phase_span=(l12 / total))
    draw_aa_subpixel_line(surface, p2[0], p2[1], p3[0], p3[1], t, color_shift, phase_start=s2, phase_span=(l23 / total))
    draw_aa_subpixel_line(surface, p3[0], p3[1], p1[0], p1[1], t, color_shift, phase_start=s3, phase_span=(l31 / total))


def inject_bouncing_ring(surface: pygame.Surface, t: float, color_shift: float, endpoint_speed: float) -> None:
    radius = 5.625
    thickness = 1.8
    c = (GRID_SIZE - 1) * 0.5
    outer = radius + thickness * 0.5
    amp_x = max(0.0, c - outer - 0.2)
    amp_y = max(0.0, c - outer - 0.2)
    s = max(0.0, endpoint_speed)
    x = c + amp_x * math.sin(t * s * 1.23 + 0.20)
    y = c + amp_y * math.sin(t * s * 1.71 + 1.10)
    draw_aa_subpixel_rainbow_ring(surface, x, y, t, color_shift, phase_dir=1.0, radius=radius, thickness=thickness)


def scalar_noise(x: float, y: float, t: float) -> float:
    n = (
        math.sin(1.73 * x + 0.91 * t)
        + math.sin(1.37 * y - 1.11 * t)
        + math.sin(1.09 * (x + y) + 0.77 * t)
    )
    return n / 3.0


def noise1d(x: float) -> float:
    # Deterministic value noise in [-1, 1] with smooth interpolation.
    x0 = math.floor(x)
    x1 = x0 + 1
    f = x - x0
    u = f * f * (3.0 - 2.0 * f)

    def rnd(i: int) -> float:
        v = math.sin(i * 127.1 + 311.7) * 43758.5453
        return (v - math.floor(v)) * 2.0 - 1.0

    n0 = rnd(int(x0))
    n1 = rnd(int(x1))
    return n0 * (1.0 - u) + n1 * u


def noise1d_hq(x: float) -> float:
    # Higher-quality 1D value noise with quintic fade interpolation.
    x0 = math.floor(x)
    x1 = x0 + 1
    f = x - x0
    u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0)

    def rnd(i: int) -> float:
        v = math.sin(i * 157.31 + 17.13) * 43758.5453123
        return (v - math.floor(v)) * 2.0 - 1.0

    n0 = rnd(int(x0))
    n1 = rnd(int(x1))
    return n0 * (1.0 - u) + n1 * u


def fbm1d_hq(x: float) -> float:
    # Multi-octave fractal noise in [-1, 1].
    total = 0.0
    amp = 1.0
    freq = 1.0
    norm = 0.0
    for _ in range(6):
        total += noise1d_hq(x * freq) * amp
        norm += amp
        amp *= 0.5
        freq *= 2.0
    return total / max(norm, 1e-6)


def noise2d_hq(x: float, y: float) -> float:
    # Higher-quality 2D value noise in [-1, 1] with quintic fade interpolation.
    x0 = math.floor(x)
    y0 = math.floor(y)
    x1 = x0 + 1
    y1 = y0 + 1
    fx = x - x0
    fy = y - y0
    ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    uy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)

    def rnd(ix: int, iy: int) -> float:
        v = math.sin(ix * 127.1 + iy * 311.7 + 74.7) * 43758.5453123
        return (v - math.floor(v)) * 2.0 - 1.0

    n00 = rnd(int(x0), int(y0))
    n10 = rnd(int(x1), int(y0))
    n01 = rnd(int(x0), int(y1))
    n11 = rnd(int(x1), int(y1))

    nx0 = n00 * (1.0 - ux) + n10 * ux
    nx1 = n01 * (1.0 - ux) + n11 * ux
    return nx0 * (1.0 - uy) + nx1 * uy


def fbm2d_hq(x: float, y: float) -> float:
    # Multi-octave 2D fractal noise in [-1, 1].
    total = 0.0
    amp = 1.0
    freq = 1.0
    norm = 0.0
    for _ in range(6):
        total += noise2d_hq(x * freq, y * freq) * amp
        norm += amp
        amp *= 0.5
        freq *= 2.0
    return total / max(norm, 1e-6)


def curl_noise(x: float, y: float, t: float) -> tuple[float, float]:
    eps = 0.05
    dn_dy = (scalar_noise(x, y + eps, t) - scalar_noise(x, y - eps, t)) / (2 * eps)
    dn_dx = (scalar_noise(x + eps, y, t) - scalar_noise(x - eps, y, t)) / (2 * eps)
    return dn_dy, -dn_dx


def transport_surface(surface: pygame.Surface, sample_fn, fraction: float = 0.75, dim: float = 1.0, glow: float = 0.0) -> None:
    src = surface.copy()
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            c0 = get_rgb(src, x, y)
            sx, sy = sample_fn(x, y)
            c1 = sample_rgb_bilinear(src, sx, sy)
            nr = (c0[0] * (1.0 - fraction) + c1[0] * fraction) * dim
            ng = (c0[1] * (1.0 - fraction) + c1[1] * fraction) * dim
            nb = (c0[2] * (1.0 - fraction) + c1[2] * fraction) * dim
            if glow > 0.0:
                vx = sx - x
                vy = sy - y
                g = clamp(math.hypot(vx, vy) * glow, 0.0, 1.0)
                nr = nr + g * 28.0
                ng = ng + g * 28.0
                nb = nb + g * 28.0
                peak = max(nr, ng, nb)
                if peak > 220.0:
                    s = 220.0 / peak
                    nr *= s
                    ng *= s
                    nb *= s
            set_rgb(surface, x, y, (int(round(nr)), int(round(ng)), int(round(nb))))


def apply_mode(surface: pygame.Surface, mode_idx: int, t: float, state: dict) -> None:
    cx = (GRID_SIZE - 1) * 0.5
    cy = (GRID_SIZE - 1) * 0.5

    if mode_idx == 0:
        # No trail processing: frame is emitter-only (buffer clear happens in main loop).
        return

    elif mode_idx == 1:
        # Chromatic split transport: R/G/B sample from different vector fields.
        src = surface.copy()
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                dx = x - cx
                dy = y - cy
                r = math.hypot(dx, dy) + 1e-6
                th = math.atan2(dy, dx)
                # R inward spiral
                sxr = cx + math.cos(th - 0.18) * min(40.0, r + 0.35)
                syr = cy + math.sin(th - 0.18) * min(40.0, r + 0.35)
                # G outward
                sxg = cx + math.cos(th) * max(0.0, r - 0.35)
                syg = cy + math.sin(th) * max(0.0, r - 0.35)
                # B rotation
                sxb = cx + math.cos(th - 0.22) * r
                syb = cy + math.sin(th - 0.22) * r
                cr = sample_rgb_bilinear(src, sxr, syr)
                cg = sample_rgb_bilinear(src, sxg, syg)
                cb = sample_rgb_bilinear(src, sxb, syb)
                set_rgb(surface, x, y, (int(cr[0]), int(cg[1]), int(cb[2])))

    elif mode_idx == 2:
        # Polar radial warp: periodic in/out scaling around grid center.
        src = surface.copy()
        pulse = 1.0 + 0.16 * math.sin(t * 2.3)
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                dx = x - cx
                dy = y - cy
                r = math.hypot(dx, dy)
                th = math.atan2(dy, dx)
                sr = r / pulse
                sx = cx + math.cos(th) * sr
                sy = cy + math.sin(th) * sr
                c = sample_rgb_bilinear(src, sx, sy)
                set_rgb(surface, x, y, (int(c[0]), int(c[1]), int(c[2])))

    elif mode_idx == 3:
        # Polar warp 2: outward-only projection by sampling from smaller radius.
        src = surface.copy()
        # Outward-only projection: sample from inner radius so content mirrors/flows outward.
        outward_scale = 1.22
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                dx = x - cx
                dy = y - cy
                r = math.hypot(dx, dy)
                th = math.atan2(dy, dx)
                sr = r / outward_scale
                sx = cx + math.cos(th) * sr
                sy = cy + math.sin(th) * sr
                c = sample_rgb_bilinear(src, sx, sy)
                set_rgb(surface, x, y, (int(c[0]), int(c[1]), int(c[2])))

    elif mode_idx == 4:
        # Event-driven shockwave: radial displacement starts when center 2x2 gets newly lit.
        trigger_time = state.get("shockwave_trigger_time", -1.0)
        if trigger_time < 0.0:
            return
        elapsed = t - trigger_time
        if elapsed < 0.0:
            return
        wave_speed = 18.0
        wave_radius = elapsed * wave_speed
        max_radius = math.hypot(cx, cy) + 1.5
        if wave_radius > max_radius:
            return
        width = 1.3
        def sample_fn(x, y):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy) + 1e-6
            ux = dx / r
            uy = dy / r
            strength = 0.15
            if abs(r - wave_radius) < width:
                strength = 1.2
            return x - ux * strength, y - uy * strength
        transport_surface(surface, sample_fn, fraction=0.82, dim=0.997)

    elif mode_idx == 5:
        # Periodic shockwave: repeats every 500 ms independent of emitter-center trigger.
        period = 0.5
        phase = (t % period) / period
        max_radius = math.hypot(cx, cy)
        wave_radius = phase * max_radius
        width = 1.1

        def sample_fn(x, y):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy) + 1e-6
            ux = dx / r
            uy = dy / r
            strength = 0.0
            if abs(r - wave_radius) < width:
                strength = 1.6
            return x - ux * strength, y - uy * strength

        transport_surface(surface, sample_fn, fraction=0.9, dim=0.998)

    elif mode_idx == 6:
        # Rotating coordinate sampling: global frame sampled in a sinusoidally rotated basis.
        src = surface.copy()
        angle = math.sin(t) * 0.3
        ca = math.cos(angle)
        sa = math.sin(angle)
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                dx = x - cx
                dy = y - cy
                sx = cx + (dx * ca - dy * sa)
                sy = cy + (dx * sa + dy * ca)
                c = sample_rgb_bilinear(src, sx, sy)
                set_rgb(surface, x, y, (int(c[0]), int(c[1]), int(c[2])))

    elif mode_idx == 7:
        # Rotating wind advection: global directional smear, wind vector rotates over time.
        # Global directional smear with slowly rotating wind direction.
        wind_angle = t * (2.0 * math.pi * 0.25)
        wind_step = 0.95
        wx = math.cos(wind_angle) * wind_step
        wy = math.sin(wind_angle) * wind_step

        def sample_fn(x, y):
            # Backward sampling along wind keeps advection stable.
            return x - wx, y - wy

        transport_surface(surface, sample_fn, fraction=0.86, dim=0.999)

    elif mode_idx == 8:
        # Rotating wind wave: linear advection + sinusoidal perpendicular wobble.
        wind_angle = t * (2.0 * math.pi * 0.25)
        wind_step = 0.95
        wx = math.cos(wind_angle)
        wy = math.sin(wind_angle)
        # Unit perpendicular to current wind direction.
        px = -wy
        py = wx
        wave_amp = 0.65
        wave_freq = 0.20
        wave_speed = 1.20

        def sample_fn(x, y):
            # 1) Backward advection along linear wind.
            sx = x - wx * wind_step
            sy = y - wy * wind_step
            # 2) Sinus wobble only along perpendicular axis.
            proj = sx * wx + sy * wy
            wobble = math.sin(proj * wave_freq + t * wave_speed) * wave_amp
            sx += px * wobble
            sy += py * wobble
            return sx, sy

        transport_surface(surface, sample_fn, fraction=0.88, dim=0.999)

    elif mode_idx == 9:
        # Meandering jet: dominant flow axis + sinusoidal transverse drift + slight turbulence.
        # Main stream direction is gently steered by 1D noise over time.
        dir_noise = scalar_noise(0.0, 0.0, t * 0.25)
        # Map noise [-1, 1] to full [0, 2pi] heading range.
        base_angle = (dir_noise + 1.0) * math.pi
        bx = math.cos(base_angle)
        by = math.sin(base_angle)
        px = -by
        py = bx
        base_speed = 1.05
        wave_freq = 0.34
        wave_speed = 1.15
        noise_freq = 0.28
        noise_speed = 0.95
        wave_amp = 0.72
        turb_amp = 0.36
        phase_drift = t * 0.42
        transport_fraction = 0.90
        transport_dim = 0.999
        hue_drift_strength = 0.085
        src = surface.copy()

        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                # Position projected onto base flow axis controls meander phase.
                s = x * bx + y * by
                sin_meander = math.sin(s * wave_freq + phase_drift + t * wave_speed) * wave_amp
                noise_meander = scalar_noise(s * noise_freq + phase_drift, 0.0, t * noise_speed) * wave_amp
                meander = sin_meander + noise_meander
                # Small turbulence keeps the stream organic without destroying the main channel.
                turb = scalar_noise(x * 0.33, y * 0.33, t * 0.8) * turb_amp
                vx = bx * base_speed + px * (meander + turb)
                vy = by * base_speed + py * (meander + turb)

                sx = x - vx
                sy = y - vy
                c0 = get_rgb(src, x, y)
                c1 = sample_rgb_bilinear(src, sx, sy)
                nr = (c0[0] * (1.0 - transport_fraction) + c1[0] * transport_fraction) * transport_dim
                ng = (c0[1] * (1.0 - transport_fraction) + c1[1] * transport_fraction) * transport_dim
                nb = (c0[2] * (1.0 - transport_fraction) + c1[2] * transport_fraction) * transport_dim

                # Hue drift along local velocity angle.
                h, sv, vv = colorsys.rgb_to_hsv(
                    clamp(nr / 255.0, 0.0, 1.0),
                    clamp(ng / 255.0, 0.0, 1.0),
                    clamp(nb / 255.0, 0.0, 1.0),
                )
                angle_norm = math.atan2(vy, vx) / (2.0 * math.pi)
                h = (h + angle_norm * hue_drift_strength) % 1.0
                rr, gg, bb = colorsys.hsv_to_rgb(h, sv, vv)
                set_rgb(surface, x, y, (int(rr * 255), int(gg * 255), int(bb * 255)))

    elif mode_idx == 10:
        # Attractor field: inverse-square pull plus tangential swirl around moving attractors.
        def sample_fn(x, y):
            dx = dy = 0.0
            at = [
                (cx + 7.0 * math.sin(t * 0.7), cy + 7.0 * math.cos(t * 0.9)),
                (cx + 6.0 * math.cos(t * 1.1), cy + 6.0 * math.sin(t * 0.8)),
            ]
            for ax, ay in at:
                vx = ax - x
                vy = ay - y
                d2 = vx * vx + vy * vy + 2.0
                inv = 1.0 / d2
                dx += vx * inv * 9.0
                dy += vy * inv * 9.0
                dx += -vy * inv * 4.0
                dy += vx * inv * 4.0
            return x - dx, y - dy
        transport_surface(surface, sample_fn, fraction=0.8, dim=0.997)

    elif mode_idx == 11:
        # Fractal spiral advection: spiral angle depends on radius and time.
        def sample_fn(x, y):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy)
            th = math.atan2(dy, dx)
            ang_step = 0.18 + 0.22 * math.sin(r * 0.45 + t * 0.8)
            sr = min(40.0, r + 0.28)
            sx = cx + math.cos(th - ang_step) * sr
            sy = cy + math.sin(th - ang_step) * sr
            return sx, sy
        transport_surface(surface, sample_fn, fraction=0.82, dim=0.998)
    elif mode_idx == 12:
        # Ring Flow: soft-blended concentric zones (inner CW swirl, middle outward drift, outer CCW swirl).
        src = surface.copy()
        max_r = math.hypot(cx, cy)
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                dx = x - cx
                dy = y - cy
                r = math.hypot(dx, dy)
                rn = r / max_r if max_r > 1e-6 else 0.0
                th = math.atan2(dy, dx)

                # Smooth radial zone weights (gaussian-like) for seamless transitions.
                w_inner = math.exp(-((rn - 0.23) / 0.17) ** 2)
                w_mid = math.exp(-((rn - 0.56) / 0.18) ** 2)
                w_outer = math.exp(-((rn - 0.86) / 0.16) ** 2)
                w_sum = w_inner + w_mid + w_outer + 1e-6
                w_inner /= w_sum
                w_mid /= w_sum
                w_outer /= w_sum

                ang = (-0.26 * w_inner) + (0.24 * w_outer)
                drift = 0.42 * w_mid

                # Backward sampling for stable advection:
                # outward drift samples slightly inward radius.
                sample_r = clamp(r - drift, 0.0, max_r + 1.5)
                sample_th = th - ang
                sx = cx + math.cos(sample_th) * sample_r
                sy = cy + math.sin(sample_th) * sample_r

                c = sample_rgb_bilinear(src, sx, sy)
                set_rgb(surface, x, y, (int(c[0]), int(c[1]), int(c[2])))

    elif mode_idx == 13:
        # Ring Flow 2: same soft concentric transport, but each zone "breathes" at its own frequency.
        src = surface.copy()
        max_r = math.hypot(cx, cy)
        # Breathing factors in [0.1, 1.0], independent per zone.
        b_inner = 0.55 + 0.45 * math.sin(t * 0.61 + 0.20)
        b_mid = 0.55 + 0.45 * math.sin(t * 0.93 + 1.10)
        b_outer = 0.55 + 0.45 * math.sin(t * 1.27 + 2.30)
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                dx = x - cx
                dy = y - cy
                r = math.hypot(dx, dy)
                rn = r / max_r if max_r > 1e-6 else 0.0
                th = math.atan2(dy, dx)

                # Dynamic zone centers/sigmas (50-100% breathing), blended smoothly (AA/subpixel).
                c_inner = 0.23 * b_inner
                c_mid = 0.56 * b_mid
                c_outer = 0.86 * b_outer
                s_inner = 0.17 * b_inner
                s_mid = 0.18 * b_mid
                s_outer = 0.16 * b_outer

                w_inner = math.exp(-((rn - c_inner) / max(0.02, s_inner)) ** 2)
                w_mid = math.exp(-((rn - c_mid) / max(0.02, s_mid)) ** 2)
                w_outer = math.exp(-((rn - c_outer) / max(0.02, s_outer)) ** 2)
                w_sum = w_inner + w_mid + w_outer + 1e-6
                w_inner /= w_sum
                w_mid /= w_sum
                w_outer /= w_sum

                ang = (-0.26 * w_inner) + (0.24 * w_outer)
                drift = 0.42 * w_mid
                sample_r = clamp(r - drift, 0.0, max_r + 1.5)
                sample_th = th - ang
                sx = cx + math.cos(sample_th) * sample_r
                sy = cy + math.sin(sample_th) * sample_r

                c = sample_rgb_bilinear(src, sx, sy)
                set_rgb(surface, x, y, (int(c[0]), int(c[1]), int(c[2])))

    elif mode_idx == 14:
        # Square spiral stream: ring-by-ring orthogonal transport on square shells.
        apply_square_spiral_tail(
            surface,
            GRID_SIZE // 2,
            GRID_SIZE // 2,
            (GRID_SIZE // 2) - 1,
            SPIRAL_TRANSPORT_FRACTION,
            SPIRAL_DIM_DEFAULT,
        )
    elif mode_idx == 15:
        # Round spiral inward: backward sample from larger radius to pull content inward.
        apply_round_spiral_tail(
            surface,
            cx,
            cy,
            math.hypot(cx, cy),
            SPIRAL_TRANSPORT_FRACTION,
            SPIRAL_DIM_DEFAULT,
            outward=False,
        )
    elif mode_idx == 16:
        # Round spiral outward: backward sample from smaller radius to push outward.
        apply_round_spiral_tail(
            surface,
            cx,
            cy,
            math.hypot(cx, cy),
            SPIRAL_TRANSPORT_FRACTION,
            SPIRAL_DIM_DEFAULT,
            outward=True,
        )
    elif mode_idx == 17:
        # To center: radial inward transport toward center for all pixels.
        apply_to_center_tail(
            surface,
            cx,
            cy,
            SPIRAL_TRANSPORT_FRACTION,
            SPIRAL_DIM_DEFAULT,
        )
    elif mode_idx == 18:
        # From center: radial outward transport from center for all pixels.
        apply_from_center_tail(
            surface,
            cx,
            cy,
            SPIRAL_TRANSPORT_FRACTION,
            SPIRAL_DIM_DEFAULT,
        )
    elif mode_idx == 19:
        # Directional noise: rotating wind advection with radius-dependent widening of the tail.
        wind_angle = t * (2.0 * math.pi * 0.25)
        wx = math.cos(wind_angle)
        wy = math.sin(wind_angle)
        px = -wy
        py = wx
        wind_step = 0.95
        max_r = math.hypot(cx, cy)

        def sample_fn(x, y):
            dx = x - cx
            dy = y - cy
            r = math.hypot(dx, dy)
            rn = clamp(r / max_r, 0.0, 1.0)
            s = x * wx + y * wy
            # Outward-shifting noise phase: fronts propagate away from emitter center.
            radial_phase = (r - t * 10.0) * 0.38
            n = noise1d(s * 0.12 + radial_phase)
            # Add short-wavelength sinus modulation on top of noise modulation.
            short_wave = math.sin(s * 1.45 + radial_phase * 2.8) * 0.48
            n += short_wave
            # Strong nonlinear widening toward the outer region.
            width_gain = rn ** 2.2
            lateral = n * (0.05 + 2.40 * width_gain)
            sx = x - (wx * wind_step + px * lateral)
            sy = y - (wy * wind_step + py * lateral)
            return sx, sy

        transport_surface(surface, sample_fn, fraction=0.88, dim=0.999)
    elif mode_idx == 20:
        # Directional wind base transport; strong noise alpha mask is applied later (after fade).
        wind_angle = t * (2.0 * math.pi * 0.25)
        wx = math.cos(wind_angle)
        wy = math.sin(wind_angle)
        wind_step = 0.95
        transport_fraction = 0.86
        transport_dim = 0.999

        # Base directional wind (same behavior as rotating wind mode).
        def sample_fn(x, y):
            return x - wx * wind_step, y - wy * wind_step

        transport_surface(surface, sample_fn, fraction=transport_fraction, dim=transport_dim)
    elif mode_idx == 21:
        # Pump: directional wind with outward-moving concentric slow-speed rings.
        wind_angle = t * (2.0 * math.pi * 0.25)
        wx = math.cos(wind_angle)
        wy = math.sin(wind_angle)
        base_step = 0.95
        ring_spacing = 5.2
        ring_out_speed = 3.2
        slow_strength = 0.94
        slow_sharpness = 8.0

        def sample_fn(x, y):
            r = math.hypot(x - cx, y - cy)
            phase = ((r - t * ring_out_speed) / ring_spacing) * (2.0 * math.pi)
            ring = 0.5 + 0.5 * math.cos(phase)
            slow_mask = ring ** slow_sharpness
            local_step = base_step * (1.0 - slow_strength * slow_mask)
            local_step = max(0.04, local_step)
            return x - wx * local_step, y - wy * local_step

        transport_surface(surface, sample_fn, fraction=0.88, dim=0.999)


def apply_mode19_noise_alpha_mask(surface: pygame.Surface, t: float, state: dict) -> None:
    # Final pass: strong HQ 2D alpha mask after all other dimming.
    wind_angle = t * (2.0 * math.pi * 0.25)
    wx = math.cos(wind_angle)
    wy = math.sin(wind_angle)
    noise_scale = 0.09
    noise_drift = 1.35
    dt = float(state.get("dt", 1.0 / FPS))
    state["noise_offset_x"] = float(state.get("noise_offset_x", 0.0)) + wx * noise_drift * dt
    state["noise_offset_y"] = float(state.get("noise_offset_y", 0.0)) + wy * noise_drift * dt
    off_x = state["noise_offset_x"]
    off_y = state["noise_offset_y"]

    src = surface.copy()
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            nx = x * noise_scale - off_x
            ny = y * noise_scale - off_y
            n = fbm2d_hq(nx, ny)
            alpha = clamp((0.5 + 0.5 * n - 0.5) * 3.6 + 0.5, 0.0, 1.0)
            dim_mask_base = 0.06 + 0.94 * alpha
            dim_mask = 1.0 * 0.875 + dim_mask_base * 0.125
            c = get_rgb(src, x, y)
            set_rgb(
                surface,
                x,
                y,
                (
                    int(clamp(c[0] * dim_mask, 0.0, 255.0)),
                    int(clamp(c[1] * dim_mask, 0.0, 255.0)),
                    int(clamp(c[2] * dim_mask, 0.0, 255.0)),
                ),
            )


def apply_global_fade(surface: pygame.Surface, fade_factor: float) -> None:
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            c = surface.get_at((x, y))
            surface.set_at((x, y), (int(round(c.r * fade_factor)), int(round(c.g * fade_factor)), int(round(c.b * fade_factor))))


def apply_blur_2x2(surface: pygame.Surface) -> None:
    src = surface.copy()
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            r_sum = 0.0
            g_sum = 0.0
            b_sum = 0.0
            for oy in (-1, 0, 1):
                sy = clamp(y + oy, 0, GRID_SIZE - 1)
                for ox in (-1, 0, 1):
                    sx = clamp(x + ox, 0, GRID_SIZE - 1)
                    c = get_rgb(src, int(sx), int(sy))
                    r_sum += c[0]
                    g_sum += c[1]
                    b_sum += c[2]
            r = r_sum / 9.0
            g = g_sum / 9.0
            b = b_sum / 9.0
            set_rgb(surface, x, y, (int(r), int(g), int(b)))


def center_4_energy(surface: pygame.Surface) -> float:
    c0 = (GRID_SIZE // 2) - 1
    c1 = GRID_SIZE // 2
    e = 0.0
    for y in (c0, c1):
        for x in (c0, c1):
            r, g, b = get_rgb(surface, x, y)
            e += r + g + b
    return e


def main() -> None:
    pygame.init()
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
    pygame.display.set_caption("Prototypes")
    clock = pygame.time.Clock()

    font = pygame.font.SysFont("arial", 16)
    font_small = pygame.font.SysFont("arial", 12)

    grid_rect = pygame.Rect(MARGIN, MARGIN, GRID_PIXELS, GRID_PIXELS)
    grid_surface = pygame.Surface((GRID_SIZE, GRID_SIZE))
    grid_surface.fill((0, 0, 0))

    ui_x = grid_rect.right + MARGIN
    panel_rect = pygame.Rect(ui_x, MARGIN, RIGHT_UI_WIDTH, WINDOW_HEIGHT - 2 * MARGIN)

    def slider_y(idx: int) -> int:
        return MARGIN + 112 + idx * 42

    sliders = [
        Slider("Endpoint Speed", 0.00, 2.00, ENDPOINT_SPEED_DEFAULT, pygame.Rect(ui_x + 20, slider_y(0), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Color Cycling Speed", 0.00, 1.00, COLOR_SHIFT_DEFAULT, pygame.Rect(ui_x + 20, slider_y(1), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Fade %", 95.0, 100.0, 99.995, pygame.Rect(ui_x + 20, slider_y(2), RIGHT_UI_WIDTH - 40, 14), 3),
    ]

    emitter_gap = 6
    emitter_btn_w = (RIGHT_UI_WIDTH - 40 - emitter_gap * 3) // 4
    emitter_line_button = Button("Line", pygame.Rect(ui_x + 20, MARGIN + 58, emitter_btn_w, 28), active=True)
    emitter_dots_button = Button("Dots", pygame.Rect(ui_x + 20 + emitter_btn_w + emitter_gap, MARGIN + 58, emitter_btn_w, 28), active=False)
    emitter_triangle_button = Button("Triangle", pygame.Rect(ui_x + 20 + (emitter_btn_w + emitter_gap) * 2, MARGIN + 58, emitter_btn_w, 28), active=False)
    emitter_ring_button = Button("Ring", pygame.Rect(ui_x + 20 + (emitter_btn_w + emitter_gap) * 3, MARGIN + 58, emitter_btn_w, 28), active=False)

    mode_buttons: list[Button] = []
    mode_grid_y = MARGIN + 240
    mode_gap_x = 8
    mode_gap_y = 4
    mode_cols = 2
    mode_btn_w = (RIGHT_UI_WIDTH - 40 - mode_gap_x) // mode_cols
    mode_btn_h = 18
    for idx, label in enumerate(MODE_BUTTON_LABELS):
        col = idx % mode_cols
        row = idx // mode_cols
        bx = ui_x + 20 + col * (mode_btn_w + mode_gap_x)
        by = mode_grid_y + row * (mode_btn_h + mode_gap_y)
        mode_buttons.append(Button(label, pygame.Rect(bx, by, mode_btn_w, mode_btn_h), active=(idx == 0)))
    mode_rows = (len(MODE_BUTTON_LABELS) + mode_cols - 1) // mode_cols
    blur_button = Button("Blur", pygame.Rect(ui_x + 20, mode_grid_y + mode_rows * (mode_btn_h + mode_gap_y) + 6, RIGHT_UI_WIDTH - 40, 24), active=False)

    state = {
        "history": [grid_surface.copy(), grid_surface.copy(), grid_surface.copy()],
        "rd": [[0.0 for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)],
        "frame": 0,
        "fade": 0.999,
        "shockwave_trigger_time": -1.0,
        "shockwave_center_latched": False,
        "prev_t": 0.0,
        "dt": 1.0 / FPS,
        "noise_offset_x": 0.0,
        "noise_offset_y": 0.0,
    }

    emitter_mode = "line"
    mode_idx = 0
    blur_enabled = False

    start_time = pygame.time.get_ticks() / 1000.0
    running = True
    while running:
        clock.tick(0)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

            if emitter_line_button.handle_event(event):
                emitter_mode = "line"
                emitter_line_button.active = True
                emitter_dots_button.active = False
                emitter_triangle_button.active = False
                emitter_ring_button.active = False
            if emitter_dots_button.handle_event(event):
                emitter_mode = "orbiting_dots"
                emitter_line_button.active = False
                emitter_dots_button.active = True
                emitter_triangle_button.active = False
                emitter_ring_button.active = False
            if emitter_triangle_button.handle_event(event):
                emitter_mode = "triangle"
                emitter_line_button.active = False
                emitter_dots_button.active = False
                emitter_triangle_button.active = True
                emitter_ring_button.active = False
            if emitter_ring_button.handle_event(event):
                emitter_mode = "ring"
                emitter_line_button.active = False
                emitter_dots_button.active = False
                emitter_triangle_button.active = False
                emitter_ring_button.active = True

            for i, btn in enumerate(mode_buttons):
                if btn.handle_event(event):
                    mode_idx = i
                    for j, other in enumerate(mode_buttons):
                        other.active = (j == mode_idx)
            if blur_button.handle_event(event):
                blur_enabled = not blur_enabled
                blur_button.active = blur_enabled

            for slider in sliders:
                slider.handle_event(event)

        t = pygame.time.get_ticks() / 1000.0 - start_time
        dt = t - float(state.get("prev_t", t))
        if dt < 0.0:
            dt = 0.0
        elif dt > 0.1:
            dt = 0.1
        state["dt"] = dt
        state["prev_t"] = t
        endpoint_speed, color_shift, fade_percent = [s.value for s in sliders]
        fade_factor = fade_percent / 100.0
        state["fade"] = fade_factor

        center_before = center_4_energy(grid_surface)
        if mode_idx == 0:
            grid_surface.fill((0, 0, 0))
            center_before = 0.0

        if emitter_mode == "line":
            inject_lissajous_line(grid_surface, t, color_shift, endpoint_speed)
        elif emitter_mode == "orbiting_dots":
            inject_orbiting_dots(grid_surface, t, endpoint_speed, color_shift)
        elif emitter_mode == "triangle":
            inject_lissajous_triangle(grid_surface, t, color_shift, endpoint_speed)
        else:
            inject_bouncing_ring(grid_surface, t, color_shift, endpoint_speed)

        if mode_idx == 4:
            center_after = center_4_energy(grid_surface)
            center_newly_drawn = (center_after - center_before) > 24.0
            if center_newly_drawn and not state["shockwave_center_latched"]:
                state["shockwave_trigger_time"] = t
                state["shockwave_center_latched"] = True
            elif not center_newly_drawn:
                state["shockwave_center_latched"] = False

        if mode_idx != 0:
            apply_mode(grid_surface, mode_idx, t, state)
            apply_global_fade(grid_surface, fade_factor)
            if mode_idx == 20:
                apply_mode19_noise_alpha_mask(grid_surface, t, state)
        if blur_enabled:
            apply_blur_2x2(grid_surface)

        # Update temporal history for echo-based modes.
        state["history"][2] = state["history"][1]
        state["history"][1] = state["history"][0]
        state["history"][0] = grid_surface.copy()
        state["frame"] += 1

        screen.fill(BG)
        scaled_grid = pygame.transform.scale(grid_surface, (GRID_PIXELS, GRID_PIXELS))
        screen.blit(scaled_grid, grid_rect.topleft)

        pygame.draw.rect(screen, PANEL, panel_rect, border_radius=10)
        title = font.render("Prototypes", True, TEXT)
        screen.blit(title, (panel_rect.left + 20, panel_rect.top + 20))
        fps_text = font.render(f"FPS: {clock.get_fps():5.1f}", True, TEXT)
        screen.blit(fps_text, (panel_rect.right - fps_text.get_width() - 20, panel_rect.top + 20))

        emitter_line_button.draw(screen, font)
        emitter_dots_button.draw(screen, font)
        emitter_triangle_button.draw(screen, font)
        emitter_ring_button.draw(screen, font)

        mode_header = font_small.render("Trail Modes", True, TEXT)
        screen.blit(mode_header, (panel_rect.left + 20, mode_grid_y - 18))
        for btn in mode_buttons:
            btn.draw(screen, font_small)
        blur_button.draw(screen, font)

        for slider in sliders:
            slider.draw(screen, font)

        pygame.display.flip()

    pygame.quit()
    sys.exit(0)


if __name__ == "__main__":
    main()