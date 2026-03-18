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
CIRCLE_DIAMETER = 1.5
ORBIT_DIAMETER = 10.0
ORBIT_ANGULAR_SPEED = 0.35
ORBIT_COLOR_SPEED = 0.10
UI_SLIDER_START_Y = 80
UI_SLIDER_GAP_Y = 70

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
            pygame.draw.rect(
                surface,
                FILL,
                (self.rect.left, self.rect.top, fill_width, self.rect.height),
                border_radius=5,
            )

        knob_x = self.rect.left + fill_width
        pygame.draw.circle(surface, KNOB, (knob_x, self.rect.centery), self.rect.height // 2 + 3)

        label = f"{self.label}: {self.value:.{self.decimals}f}"
        text_surface = font.render(label, True, TEXT)
        surface.blit(text_surface, (self.rect.left, self.rect.top - 20))


def sample_profile(
    noise: Perlin1D, t: float, speed: float, amplitude: float, scale: float, count: int
) -> list[float]:
    values = []
    phase = t * speed
    freq = 0.23
    for i in range(count):
        n = noise.noise(i * freq * scale + phase)
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
    center_y = rect.centery
    pygame.draw.line(surface, AXIS_COLOR, (rect.left + 12, center_y), (rect.right - 12, center_y), 2)

    if len(values) < 2:
        return

    amp_px = max(6, int((rect.height * 0.42) * min(1.0, amplitude)))
    points = []
    for i, val in enumerate(values):
        x = rect.left + 12 + i * ((rect.width - 24) / (len(values) - 1))
        y = center_y - val * amp_px
        points.append((x, y))
    pygame.draw.lines(surface, GRAPH_X_COLOR, False, points, 3)

    surface.blit(font.render(title, True, TEXT), (rect.left + 12, rect.top + 8))


def draw_y_graph(surface: pygame.Surface, rect: pygame.Rect, values: list[float], amplitude: float, title: str, font: pygame.font.Font) -> None:
    pygame.draw.rect(surface, PANEL, rect, border_radius=10)
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


def rainbow_color(t: float) -> tuple[int, int, int]:
    hue = (t * 0.08) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


def rainbow_color_with_phase(t: float, speed: float, phase: float) -> tuple[int, int, int]:
    hue = (t * speed + phase) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


def draw_antialiased_subpixel_circle(
    grid_surface: pygame.Surface, cx: float, cy: float, diameter: float, color: tuple[int, int, int]
) -> None:
    radius = diameter * 0.5
    min_x = max(0, int(math.floor(cx - radius - 1.0)))
    max_x = min(GRID_SIZE - 1, int(math.ceil(cx + radius + 1.0)))
    min_y = max(0, int(math.floor(cy - radius - 1.0)))
    max_y = min(GRID_SIZE - 1, int(math.ceil(cy + radius + 1.0)))

    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            px = x + 0.5
            py = y + 0.5
            dist = math.hypot(px - cx, py - cy)
            coverage = max(0.0, min(1.0, radius + 0.5 - dist))
            if coverage <= 0.0:
                continue

            old = grid_surface.get_at((x, y))
            nr = int(old.r * (1.0 - coverage) + color[0] * coverage)
            ng = int(old.g * (1.0 - coverage) + color[1] * coverage)
            nb = int(old.b * (1.0 - coverage) + color[2] * coverage)
            grid_surface.set_at((x, y), (nr, ng, nb))


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
        Slider("X Geschwindigkeit", -2.00, 2.00, -1.73, pygame.Rect(ui_x + 20, slider_y(0), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("X Amplitude", 0.10, 1.00, 1.00, pygame.Rect(ui_x + 20, slider_y(1), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Y Geschwindigkeit", -2.00, 2.00, -1.72, pygame.Rect(ui_x + 20, slider_y(2), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Y Amplitude", 0.10, 1.00, 1.00, pygame.Rect(ui_x + 20, slider_y(3), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Dimmen %", 99.0, 100.0, 99.922, pygame.Rect(ui_x + 20, slider_y(4), RIGHT_UI_WIDTH - 40, 14), 3),
        Slider("Scale X", 0.10, 4.00, 0.33, pygame.Rect(ui_x + 20, slider_y(5), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Scale Y", 0.10, 4.00, 0.32, pygame.Rect(ui_x + 20, slider_y(6), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Orbit Speed", -2.00, 2.00, ORBIT_ANGULAR_SPEED, pygame.Rect(ui_x + 20, slider_y(7), RIGHT_UI_WIDTH - 40, 14), 2),
        Slider("Color Speed", -1.00, 1.00, ORBIT_COLOR_SPEED, pygame.Rect(ui_x + 20, slider_y(8), RIGHT_UI_WIDTH - 40, 14), 2),
    ]

    noise_x = Perlin1D(seed=42)
    noise_y = Perlin1D(seed=1337)

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

        x_speed, x_amp, y_speed, y_amp, fade_percent, x_scale, y_scale, orbit_speed, color_speed = [s.value for s in sliders]
        x_profile = sample_profile(noise_x, t, x_speed, x_amp, x_scale, GRID_SIZE)
        y_profile = sample_profile(noise_y, t, y_speed, y_amp, y_scale, GRID_SIZE)
        fade_factor = fade_percent / 100.0

        orbit_center = (GRID_SIZE * 0.5) - 0.5
        orbit_radius = ORBIT_DIAMETER * 0.5
        base_angle = t * orbit_speed
        for i in range(3):
            angle = base_angle + (i * (2.0 * math.pi / 3.0))
            circle_x = orbit_center + math.cos(angle) * orbit_radius
            circle_y = orbit_center + math.sin(angle) * orbit_radius
            color = rainbow_color_with_phase(t, color_speed, i / 3.0)
            draw_antialiased_subpixel_circle(grid_surface, circle_x, circle_y, CIRCLE_DIAMETER, color)
        advect_axes_and_dim(grid_surface, x_profile, y_profile, fade_factor)

        screen.fill(BG)
        draw_grid(screen, grid_rect, x_profile, y_profile)
        scaled_grid = pygame.transform.scale(grid_surface, (GRID_PIXELS, GRID_PIXELS))
        screen.blit(scaled_grid, grid_rect.topleft)
        draw_x_graph(screen, x_graph_rect, x_profile, x_amp, "X-Achse: Perlin Noise", font)
        draw_y_graph(screen, y_graph_rect, y_profile, y_amp, "Y-Achse: Perlin Noise", font)

        pygame.draw.rect(screen, PANEL, panel_rect, border_radius=10)
        title = font.render("Steuerung", True, TEXT)
        screen.blit(title, (panel_rect.left + 20, panel_rect.top + 20))
        hint = small_font.render("9 Slider: Speed, Amp, Dimmen, Scale, Orbit, Color", True, TEXT)
        screen.blit(hint, (panel_rect.left + 20, panel_rect.top + 46))

        for slider in sliders:
            slider.draw(screen, font)

        pygame.display.flip()

    pygame.quit()
    sys.exit(0)


if __name__ == "__main__":
    main()
