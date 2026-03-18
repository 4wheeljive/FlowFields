import math
import random
import sys
import colorsys
from dataclasses import dataclass

import pygame

GRID_SIZE = 32
CELL_SIZE = 16
GRID_PIXELS = GRID_SIZE * CELL_SIZE
LEFT_GRAPH_WIDTH = 170
BOTTOM_GRAPH_HEIGHT = 170
RIGHT_UI_WIDTH = 310
MARGIN = 20
FPS = 60
ROW_SHIFT_PIXELS = 1.8
COL_SHIFT_PIXELS = 1.8
DEFAULT_FADE_PER_FRAME = 0.99
COLOR_SHIFT_DEFAULT = 0.10
VARIATION_SPEED_DEFAULT = 1.00
UI_SLIDER_START_Y = 80
UI_SLIDER_GAP_Y = 62

WINDOW_WIDTH = MARGIN * 3 + LEFT_GRAPH_WIDTH + GRID_PIXELS + RIGHT_UI_WIDTH
WINDOW_HEIGHT = MARGIN * 3 + GRID_PIXELS + BOTTOM_GRAPH_HEIGHT

BG = (15, 18, 24)
GRID_BG = (24, 29, 40)
GRID_LINE = (42, 49, 64)
GRID_VALUE = (68, 210, 255)
AXIS_COLOR = (230, 230, 240)
GRAPH_X_COLOR = (255, 176, 60)
GRAPH_Y_COLOR = (92, 245, 174)
TEXT = (230, 230, 240)
TRACK = (50, 56, 70)
FILL = (70, 130, 255)
KNOB = (235, 239, 248)
PANEL = (22, 26, 36)
GRAPH_GRID = (44, 50, 64)


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


class Perlin1D:
    def __init__(self, seed: int):
        rng = random.Random(seed)
        p = list(range(256))
        rng.shuffle(p)
        self.perm = p + p

    @staticmethod
    def _fade(t: float) -> float:
        return t * t * t * (t * (t * 6 - 15) + 10)

    @staticmethod
    def _lerp(a: float, b: float, t: float) -> float:
        return a + t * (b - a)

    def _grad(self, h: int, x: float) -> float:
        return x if (h & 1) == 0 else -x

    def noise(self, x: float) -> float:
        xi = math.floor(x) & 255
        xf = x - math.floor(x)
        u = self._fade(xf)
        a = self.perm[xi]
        b = self.perm[xi + 1]
        return self._lerp(self._grad(a, xf), self._grad(b, xf - 1.0), u)


class Perlin2D:
    def __init__(self, seed: int):
        rng = random.Random(seed)
        p = list(range(256))
        rng.shuffle(p)
        self.perm = p + p

    @staticmethod
    def _fade(t: float) -> float:
        return t * t * t * (t * (t * 6 - 15) + 10)

    @staticmethod
    def _lerp(a: float, b: float, t: float) -> float:
        return a + t * (b - a)

    def _grad(self, h: int, x: float, y: float) -> float:
        h = h & 7
        if h == 0:
            return x + y
        if h == 1:
            return -x + y
        if h == 2:
            return x - y
        if h == 3:
            return -x - y
        if h == 4:
            return x
        if h == 5:
            return -x
        if h == 6:
            return y
        return -y

    def noise(self, x: float, y: float) -> float:
        xi = math.floor(x) & 255
        yi = math.floor(y) & 255
        xf = x - math.floor(x)
        yf = y - math.floor(y)
        u = self._fade(xf)
        v = self._fade(yf)

        aa = self.perm[self.perm[xi] + yi]
        ab = self.perm[self.perm[xi] + yi + 1]
        ba = self.perm[self.perm[xi + 1] + yi]
        bb = self.perm[self.perm[xi + 1] + yi + 1]

        x1 = self._lerp(self._grad(aa, xf, yf), self._grad(ba, xf - 1.0, yf), u)
        x2 = self._lerp(self._grad(ab, xf, yf - 1.0), self._grad(bb, xf - 1.0, yf - 1.0), u)
        return self._lerp(x1, x2, v)


@dataclass
class Slider:
    label: str
    min_value: float
    max_value: float
    value: float
    rect: pygame.Rect
    decimals: int = 2
    dragging: bool = False
    display_value: float | None = None

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

        shown_value = self.display_value if self.display_value is not None else self.value
        t = (shown_value - self.min_value) / (self.max_value - self.min_value)
        fill_width = int(self.rect.width * t)
        if fill_width > 0:
            pygame.draw.rect(
                surface,
                FILL,
                (self.rect.left, self.rect.top, fill_width, self.rect.height),
                border_radius=5,
            )

        knob_x = self.rect.left + fill_width
        pygame.draw.circle(surface, KNOB, (knob_x, self.rect.centery), self.rect.height // 2 + 3)

        label = f"{self.label}: {shown_value:.{self.decimals}f}"
        text_surface = font.render(label, True, TEXT)
        surface.blit(text_surface, (self.rect.left, self.rect.top - 20))


def sample_profile(
    noise: Perlin2D, t: float, speed: float, amplitude: float, scale: float, count: int
) -> list[float]:
    values = []
    spatial_freq = 0.23
    scroll_y = t * speed
    for i in range(count):
        n = noise.noise(i * spatial_freq * scale, scroll_y)
        values.append(max(-1.0, min(1.0, n * amplitude)))
    return values


def draw_grid(surface: pygame.Surface, rect: pygame.Rect, x_values: list[float], y_values: list[float]) -> None:
    _ = x_values, y_values
    pygame.draw.rect(surface, (0, 0, 0), rect)


def advect_axes_and_dim(
    grid_surface: pygame.Surface, x_values: list[float], y_values: list[float], fade_factor: float
) -> None:
    src = grid_surface.copy()
    rows_shifted = pygame.Surface((GRID_SIZE, GRID_SIZE))

    # Pass 1: Horizontal shift per row based on Y-axis noise.
    for y in range(GRID_SIZE):
        shift = y_values[y] * ROW_SHIFT_PIXELS
        for x in range(GRID_SIZE):
            sample_x = (x - shift) % GRID_SIZE
            x0 = int(math.floor(sample_x)) % GRID_SIZE
            x1 = (x0 + 1) % GRID_SIZE
            frac = sample_x - math.floor(sample_x)

            c0 = src.get_at((x0, y))
            c1 = src.get_at((x1, y))
            r = (c0.r * (1.0 - frac)) + (c1.r * frac)
            g = (c0.g * (1.0 - frac)) + (c1.g * frac)
            b = (c0.b * (1.0 - frac)) + (c1.b * frac)
            rows_shifted.set_at((x, y), (int(r), int(g), int(b)))

    # Pass 2: Vertical shift per column based on X-axis noise, then dim.
    for x in range(GRID_SIZE):
        shift = x_values[x] * COL_SHIFT_PIXELS
        for y in range(GRID_SIZE):
            sample_y = (y - shift) % GRID_SIZE
            y0 = int(math.floor(sample_y)) % GRID_SIZE
            y1 = (y0 + 1) % GRID_SIZE
            frac = sample_y - math.floor(sample_y)

            c0 = rows_shifted.get_at((x, y0))
            c1 = rows_shifted.get_at((x, y1))

            r = ((c0.r * (1.0 - frac)) + (c1.r * frac)) * fade_factor
            g = ((c0.g * (1.0 - frac)) + (c1.g * frac)) * fade_factor
            b = ((c0.b * (1.0 - frac)) + (c1.b * frac)) * fade_factor

            grid_surface.set_at((x, y), (int(r), int(g), int(b)))


def draw_x_graph(surface: pygame.Surface, rect: pygame.Rect, values: list[float], amplitude: float, title: str, font: pygame.font.Font) -> None:
    pygame.draw.rect(surface, PANEL, rect, border_radius=10)
    for i in range(GRID_SIZE):
        x = rect.left + 12 + i * ((rect.width - 24) / (GRID_SIZE - 1))
        pygame.draw.line(surface, GRAPH_GRID, (x, rect.top + 12), (x, rect.bottom - 12), 1)

    center_y = rect.centery
    pygame.draw.line(surface, AXIS_COLOR, (rect.left + 12, center_y), (rect.right - 12, center_y), 2)

    if len(values) < 2:
        return

    amp_px = max(6, int((rect.height * 0.42) * min(1.0, amplitude)))
    points = []
    for i, val in enumerate(values):
        x = rect.left + 12 + i * ((rect.width - 24) / (len(values) - 1))
        y = center_y + val * amp_px
        points.append((x, y))
    pygame.draw.lines(surface, GRAPH_X_COLOR, False, points, 3)

    surface.blit(font.render(title, True, TEXT), (rect.left + 12, rect.top + 8))


def draw_y_graph(surface: pygame.Surface, rect: pygame.Rect, values: list[float], amplitude: float, title: str, font: pygame.font.Font) -> None:
    pygame.draw.rect(surface, PANEL, rect, border_radius=10)
    for i in range(GRID_SIZE):
        y = rect.top + 12 + i * ((rect.height - 24) / (GRID_SIZE - 1))
        pygame.draw.line(surface, GRAPH_GRID, (rect.left + 12, y), (rect.right - 12, y), 1)

    center_x = rect.centerx
    pygame.draw.line(surface, AXIS_COLOR, (center_x, rect.top + 12), (center_x, rect.bottom - 12), 2)

    if len(values) < 2:
        return

    amp_px = max(6, int((rect.width * 0.42) * min(1.0, amplitude)))
    points = []
    for i, val in enumerate(values):
        y = rect.top + 12 + i * ((rect.height - 24) / (len(values) - 1))
        x = center_x + val * amp_px
        points.append((x, y))
    pygame.draw.lines(surface, GRAPH_Y_COLOR, False, points, 3)

    surface.blit(font.render(title, True, TEXT), (rect.left + 12, rect.top + 8))


def rainbow_color_with_phase(t: float, speed: float, phase: float) -> tuple[int, int, int]:
    hue = (t * speed + phase) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


def blend_subpixel(grid_surface: pygame.Surface, x: float, y: float, color: tuple[int, int, int]) -> None:
    x0 = int(math.floor(x))
    y0 = int(math.floor(y))
    fx = x - x0
    fy = y - y0

    for ox, wx in ((0, 1.0 - fx), (1, fx)):
        for oy, wy in ((0, 1.0 - fy), (1, fy)):
            px = x0 + ox
            py = y0 + oy
            if not (0 <= px < GRID_SIZE and 0 <= py < GRID_SIZE):
                continue
            w = wx * wy
            if w <= 0.0:
                continue
            old = grid_surface.get_at((px, py))
            nr = int(old.r * (1.0 - w) + color[0] * w)
            ng = int(old.g * (1.0 - w) + color[1] * w)
            nb = int(old.b * (1.0 - w) + color[2] * w)
            grid_surface.set_at((px, py), (nr, ng, nb))


def blend_pixel_weighted(grid_surface: pygame.Surface, px: int, py: int, color: tuple[int, int, int], w: float) -> None:
    if not (0 <= px < GRID_SIZE and 0 <= py < GRID_SIZE):
        return
    w = max(0.0, min(1.0, w))
    if w <= 0.0:
        return
    old = grid_surface.get_at((px, py))
    nr = int(old.r * (1.0 - w) + color[0] * w)
    ng = int(old.g * (1.0 - w) + color[1] * w)
    nb = int(old.b * (1.0 - w) + color[2] * w)
    grid_surface.set_at((px, py), (nr, ng, nb))


def draw_aa_endpoint_disc(grid_surface: pygame.Surface, cx: float, cy: float, color: tuple[int, int, int], radius: float = 0.75) -> None:
    min_x = max(0, int(math.floor(cx - radius - 1.0)))
    max_x = min(GRID_SIZE - 1, int(math.ceil(cx + radius + 1.0)))
    min_y = max(0, int(math.floor(cy - radius - 1.0)))
    max_y = min(GRID_SIZE - 1, int(math.ceil(cy + radius + 1.0)))
    for py in range(min_y, max_y + 1):
        for px in range(min_x, max_x + 1):
            dx = (px + 0.5) - cx
            dy = (py + 0.5) - cy
            dist = math.hypot(dx, dy)
            # 1px soft edge for antialiasing.
            w = max(0.0, min(1.0, radius + 0.5 - dist))
            blend_pixel_weighted(grid_surface, px, py, color, w)


def draw_aa_subpixel_line(
    grid_surface: pygame.Surface,
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    t: float,
    color_shift: float,
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
        color = rainbow_color_with_phase(t, color_shift, u)

        blend_pixel_weighted(grid_surface, int(xi), int(yi), color, (1.0 - fx) * (1.0 - fy))
        blend_pixel_weighted(grid_surface, int(xi + 1), int(yi), color, fx * (1.0 - fy))
        blend_pixel_weighted(grid_surface, int(xi), int(yi + 1), color, (1.0 - fx) * fy)
        blend_pixel_weighted(grid_surface, int(xi + 1), int(yi + 1), color, fx * fy)


def inject_rainbow_two_rect_lines(grid_surface: pygame.Surface, t: float, color_shift: float) -> None:
    line_points = []
    # Full outer rectangle traversal
    for x in range(GRID_SIZE):
        line_points.append((x, 0))
    for y in range(1, GRID_SIZE):
        line_points.append((GRID_SIZE - 1, y))
    for x in range(GRID_SIZE - 2, -1, -1):
        line_points.append((x, GRID_SIZE - 1))
    for y in range(1, GRID_SIZE):
        line_points.append((0, GRID_SIZE - 1 - y))

    total = len(line_points)
    for i, (x, y) in enumerate(line_points):
        color = rainbow_color_with_phase(t, color_shift, i / total)
        grid_surface.set_at((x, y), color)


def main() -> None:
    pygame.init()
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
    pygame.display.set_caption("Perlin Grid Visualisierung")
    clock = pygame.time.Clock()

    font = pygame.font.SysFont("arial", 18)
    small_font = pygame.font.SysFont("arial", 15)

    grid_rect = pygame.Rect(MARGIN + LEFT_GRAPH_WIDTH + MARGIN, MARGIN, GRID_PIXELS, GRID_PIXELS)
    x_graph_rect = pygame.Rect(grid_rect.left, grid_rect.bottom + MARGIN, GRID_PIXELS, BOTTOM_GRAPH_HEIGHT)
    y_graph_rect = pygame.Rect(MARGIN, grid_rect.top, LEFT_GRAPH_WIDTH, GRID_PIXELS)
    grid_surface = pygame.Surface((GRID_SIZE, GRID_SIZE))
    grid_surface.fill((0, 0, 0))

    ui_x = grid_rect.right + MARGIN
    panel_rect = pygame.Rect(ui_x, MARGIN, RIGHT_UI_WIDTH, WINDOW_HEIGHT - 2 * MARGIN)
    slider_y = lambda idx: MARGIN + UI_SLIDER_START_Y + idx * UI_SLIDER_GAP_Y

    sliders = [
        Slider("X Speed", -2.00, 2.00, 0.10, pygame.Rect(ui_x + 20, slider_y(0), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("X Amplitude", 0.10, 1.00, 0.75, pygame.Rect(ui_x + 20, slider_y(1), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("X Frequency", 0.10, 4.00, 0.33, pygame.Rect(ui_x + 20, slider_y(2), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Y Speed", -2.00, 2.00, 0.10, pygame.Rect(ui_x + 20, slider_y(3), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Y Amplitude", 0.10, 1.00, 0.75, pygame.Rect(ui_x + 20, slider_y(4), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Y Frequency", 0.10, 4.00, 0.32, pygame.Rect(ui_x + 20, slider_y(5), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Variation Intensity", 0.00, 4.00, 4.00, pygame.Rect(ui_x + 20, slider_y(6), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Variation Speed", 0.00, 4.00, VARIATION_SPEED_DEFAULT, pygame.Rect(ui_x + 20, slider_y(7), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Color Shift", 0.00, 1.00, COLOR_SHIFT_DEFAULT, pygame.Rect(ui_x + 20, slider_y(8), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Fade %", 90.0, 99.999, 99.922, pygame.Rect(ui_x + 20, slider_y(9), RIGHT_UI_WIDTH - 40, 14), 3),
    ]

    noise_x = Perlin2D(seed=42)
    noise_y = Perlin2D(seed=1337)
    amp_var_x = Perlin1D(seed=101)
    amp_var_y = Perlin1D(seed=202)

    start_time = pygame.time.get_ticks() / 1000.0
    running = True
    while running:
        dt = clock.tick(FPS) / 1000.0
        _ = dt

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            for slider in sliders:
                slider.handle_event(event)

        t = pygame.time.get_ticks() / 1000.0 - start_time

        x_speed, x_amp, x_scale, y_speed, y_amp, y_scale, variation_intensity, variation_speed, color_shift, fade_percent = [s.value for s in sliders]

        # Two independent slow 1D noises drive amplitude variation.
        n_var_x = amp_var_x.noise(t * 0.16 * variation_speed)
        n_var_y = amp_var_y.noise(t * 0.13 * variation_speed + 17.0)

        # Variation modulates itself using the same random drivers.
        self_mod = 0.5 + 0.5 * ((n_var_x + n_var_y) * 0.5)
        eff_variation = variation_intensity * self_mod

        x_amp_eff = clamp(x_amp + (n_var_x * 0.45 * eff_variation), 0.10, 1.00)
        y_amp_eff = clamp(y_amp + (n_var_y * 0.45 * eff_variation), 0.10, 1.00)

        # Show live effective amplitude values in the slider labels.
        sliders[1].display_value = x_amp_eff
        sliders[4].display_value = y_amp_eff

        x_profile = sample_profile(noise_x, t, x_speed, x_amp_eff, x_scale, GRID_SIZE)
        x_profile = list(reversed(x_profile))
        y_profile = sample_profile(noise_y, t, y_speed, y_amp_eff, y_scale, GRID_SIZE)
        fade_factor = fade_percent / 100.0

        inject_rainbow_two_rect_lines(grid_surface, t, color_shift)
        advect_axes_and_dim(grid_surface, x_profile, y_profile, fade_factor)

        screen.fill(BG)
        draw_grid(screen, grid_rect, x_profile, y_profile)
        scaled_grid = pygame.transform.scale(grid_surface, (GRID_PIXELS, GRID_PIXELS))
        screen.blit(scaled_grid, grid_rect.topleft)
        draw_x_graph(screen, x_graph_rect, x_profile, x_amp_eff, "x controls columns", font)
        draw_y_graph(screen, y_graph_rect, y_profile, y_amp_eff, "y controls rows", font)

        pygame.draw.rect(screen, PANEL, panel_rect, border_radius=10)
        title = font.render("Control Parameter", True, TEXT)
        screen.blit(title, (panel_rect.left + 20, panel_rect.top + 20))

        for slider in sliders:
            slider.draw(screen, font)

        pygame.display.flip()

    pygame.quit()
    sys.exit(0)


if __name__ == "__main__":
    main()