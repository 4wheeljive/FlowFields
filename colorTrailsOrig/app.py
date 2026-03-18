import math
import sys
import time
from dataclasses import dataclass, field

import pygame

try:
    from opensimplex import OpenSimplex
except ImportError:
    print("Missing dependency: opensimplex")
    print("Install with: pip install opensimplex pygame")
    sys.exit(1)


GRID_SIZE = 64
PANEL_COUNT = 5  # base + 4 sequential stages
LEFT_MARGIN = 16
TOP_MARGIN = 16
RIGHT_PANEL_WIDTH = 420
WINDOW_WIDTH = 1360
WINDOW_HEIGHT = 900
MIN_WINDOW_WIDTH = 1080
MIN_WINDOW_HEIGHT = 760
FPS = 60

NOISE_SCALE = 1.6
NOISE_OCTAVES = 2
NOISE_LACUNARITY = 2.0
NOISE_GAIN = 0.5

COLOR_BG = (18, 18, 22)
COLOR_PANEL_BG = (30, 30, 36)
COLOR_BORDER = (70, 70, 85)
COLOR_TEXT = (225, 225, 232)
COLOR_ACCENT = (85, 170, 255)
COLOR_TRACK = (60, 60, 70)
COLOR_KNOB = (220, 220, 230)
COLOR_BUTTON = (52, 52, 62)
COLOR_BUTTON_ACTIVE = (74, 124, 189)
COLOR_HINT = (170, 170, 180)


def clamp(value, low, high):
    return max(low, min(high, value))


def fractal_noise3(noise_gen, x, y, z):
    freq = 1.0
    amp = 1.0
    total = 0.0
    amp_sum = 0.0
    for _ in range(NOISE_OCTAVES):
        total += noise_gen.noise3(x * freq, y * freq, z * freq) * amp
        amp_sum += amp
        freq *= NOISE_LACUNARITY
        amp *= NOISE_GAIN
    return total / amp_sum if amp_sum else 0.0


@dataclass
class Slider:
    label: str
    min_value: float
    max_value: float
    value: float
    rect: pygame.Rect
    decimals: int = 3
    dragging: bool = False

    def set_from_mouse(self, mx):
        t = (mx - self.rect.left) / self.rect.width
        t = clamp(t, 0.0, 1.0)
        self.value = self.min_value + t * (self.max_value - self.min_value)

    def handle_event(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            if self.rect.collidepoint(event.pos):
                self.dragging = True
                self.set_from_mouse(event.pos[0])
                return True
        if event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            self.dragging = False
        if event.type == pygame.MOUSEMOTION and self.dragging:
            self.set_from_mouse(event.pos[0])
            return True
        return False

    def draw(self, surface, font):
        pygame.draw.rect(surface, COLOR_TRACK, self.rect, border_radius=5)
        t = (self.value - self.min_value) / (self.max_value - self.min_value)
        knob_x = int(self.rect.left + t * self.rect.width)
        pygame.draw.rect(surface, COLOR_ACCENT, (self.rect.left, self.rect.top, knob_x - self.rect.left, self.rect.height), border_radius=5)
        pygame.draw.circle(surface, COLOR_KNOB, (knob_x, self.rect.centery), self.rect.height // 2 + 2)

        fmt = f"{{:.{self.decimals}f}}"
        label_text = f"{self.label}: {fmt.format(self.value)}"
        text_surf = font.render(label_text, True, COLOR_TEXT)
        surface.blit(text_surf, (self.rect.left, self.rect.top - 18))


@dataclass
class Button:
    label: str
    rect: pygame.Rect
    active: bool = False

    def handle_event(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and self.rect.collidepoint(event.pos):
            return True
        return False

    def draw(self, surface, font):
        color = COLOR_BUTTON_ACTIVE if self.active else COLOR_BUTTON
        pygame.draw.rect(surface, color, self.rect, border_radius=6)
        pygame.draw.rect(surface, COLOR_BORDER, self.rect, 1, border_radius=6)
        txt = font.render(self.label, True, COLOR_TEXT)
        tx = self.rect.centerx - txt.get_width() // 2
        ty = self.rect.centery - txt.get_height() // 2
        surface.blit(txt, (tx, ty))


EFFECT_TYPES = ["twist", "wobble", "none"]


@dataclass
class EffectSlot:
    name: str
    effect_type: str = "twist"
    sliders: dict = field(default_factory=dict)
    type_button: Button = None
    up_button: Button = None
    down_button: Button = None


def apply_twist(x, y, params):
    cx = params["center_x"].value
    cy = params["center_y"].value
    spiral_factor = params["spiral_factor"].value
    spiral_angle_deg = params["spiral_angle"].value

    dx = x - cx
    dy = y - cy
    r = math.hypot(dx, dy)
    a = math.atan2(dy, dx)
    a += math.radians(spiral_angle_deg) + r * spiral_factor
    return cx + math.cos(a) * r, cy + math.sin(a) * r


def apply_wobble(x, y, z, params):
    x_freq = params["x_freq"].value
    y_freq = params["y_freq"].value
    x_amp = params["x_amp"].value
    y_amp = params["y_amp"].value

    x2 = x + math.sin(y * y_freq + z * 0.9) * x_amp
    y2 = y + math.sin(x * x_freq + z * 1.1) * y_amp
    return x2, y2


def build_slots(side_left):
    slots = []
    slider_w = RIGHT_PANEL_WIDTH - 70
    slider_h = 10
    section_top = 90
    section_h = 190

    for i in range(4):
        y0 = section_top + i * section_h
        slot = EffectSlot(name=f"Effect {i + 1}")

        type_btn = Button(
            label="Type: twist",
            rect=pygame.Rect(side_left + 16, y0, 150, 28),
        )
        up_btn = Button(label="Up", rect=pygame.Rect(side_left + 176, y0, 62, 28))
        down_btn = Button(label="Down", rect=pygame.Rect(side_left + 246, y0, 72, 28))
        slot.type_button = type_btn
        slot.up_button = up_btn
        slot.down_button = down_btn

        sy = y0 + 48
        gap = 19
        slot.sliders = {
            "center_x": Slider("center_x", -2.0, 2.0, 0.0, pygame.Rect(side_left + 16, sy + gap * 0, slider_w, slider_h), 3),
            "center_y": Slider("center_y", -2.0, 2.0, 0.0, pygame.Rect(side_left + 16, sy + gap * 1, slider_w, slider_h), 3),
            "spiral_factor": Slider("spiral_factor", -8.0, 8.0, 1.2 if i in (0, 2) else -1.2, pygame.Rect(side_left + 16, sy + gap * 2, slider_w, slider_h), 3),
            "spiral_angle": Slider("spiral_angle", -180.0, 180.0, 0.0, pygame.Rect(side_left + 16, sy + gap * 3, slider_w, slider_h), 1),
            "x_freq": Slider("x_freq", 0.0, 15.0, 3.0, pygame.Rect(side_left + 16, sy + gap * 4, slider_w, slider_h), 3),
            "y_freq": Slider("y_freq", 0.0, 15.0, 3.0, pygame.Rect(side_left + 16, sy + gap * 5, slider_w, slider_h), 3),
            "x_amp": Slider("x_amp", 0.0, 1.5, 0.0 if i % 2 == 0 else 0.22, pygame.Rect(side_left + 16, sy + gap * 6, slider_w, slider_h), 3),
            "y_amp": Slider("y_amp", 0.0, 1.5, 0.0 if i % 2 == 0 else 0.22, pygame.Rect(side_left + 16, sy + gap * 7, slider_w, slider_h), 3),
        }
        slots.append(slot)

    return slots


def layout_ui(slots, master_speed, window_w, window_h):
    side_w = int(clamp(window_w * 0.31, 340, 460))
    side_left = window_w - side_w - 16
    side_rect = pygame.Rect(side_left, 8, side_w, window_h - 16)

    # Compact preview group: 3 columns x 2 rows (5 panels used).
    preview_left = LEFT_MARGIN
    preview_right = side_left - 12
    preview_w = max(280, preview_right - preview_left)
    preview_top = TOP_MARGIN
    preview_h = max(250, int(window_h * 0.47))
    cols = 3
    rows = 2
    gap = 10
    cell_w = (preview_w - gap * (cols - 1)) // cols
    cell_h = (preview_h - gap * (rows - 1)) // rows
    panel_size = int(clamp(min(cell_w, cell_h), 88, 240))

    group_w = panel_size * cols + gap * (cols - 1)
    start_x = preview_left + max(0, (preview_w - group_w) // 2)
    panel_rects = []
    for i in range(PANEL_COUNT):
        row = i // cols
        col = i % cols
        panel_rects.append(
            pygame.Rect(
                start_x + col * (panel_size + gap),
                preview_top + row * (panel_size + gap),
                panel_size,
                panel_size,
            )
        )

    master_speed.rect = pygame.Rect(side_left + 16, 48, side_w - 32, 12)

    slider_w = side_w - 32
    section_top = 90
    section_h = 160
    gap_y = 14
    slider_order = ["center_x", "center_y", "spiral_factor", "spiral_angle", "x_freq", "y_freq", "x_amp", "y_amp"]
    for i, slot in enumerate(slots):
        y0 = section_top + i * section_h
        slot.type_button.rect = pygame.Rect(side_left + 16, y0, 145, 28)
        slot.up_button.rect = pygame.Rect(side_left + 170, y0, 62, 28)
        slot.down_button.rect = pygame.Rect(side_left + 240, y0, 72, 28)
        sy = y0 + 48
        for idx, key in enumerate(slider_order):
            slot.sliders[key].rect = pygame.Rect(side_left + 16, sy + gap_y * idx, slider_w, 10)

    info_y = preview_top + rows * panel_size + (rows - 1) * gap + 10
    return side_rect, panel_rects, info_y


def swap_slots(slots, i, j):
    if j < 0 or j >= len(slots):
        return
    slots[i], slots[j] = slots[j], slots[i]


def stage_labels(slots):
    labels = ["Base"]
    chain = []
    for slot in slots:
        chain.append(slot.effect_type)
        labels.append(" + ".join(chain))
    return labels


def main():
    pygame.init()
    pygame.display.set_caption("64x64 Scrolling Simplex Field - Effect Chain")
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT), pygame.RESIZABLE)
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("Menlo", 14)
    title_font = pygame.font.SysFont("Menlo", 18, bold=True)
    noise = OpenSimplex(seed=int(time.time()))

    slots = build_slots(0)
    master_speed = Slider(
        "master_speed",
        0.0,
        3.0,
        0.45,
        pygame.Rect(0, 0, 120, 12),
        3,
    )

    stage_surfaces = [pygame.Surface((GRID_SIZE, GRID_SIZE)) for _ in range(PANEL_COUNT)]

    running = True
    while running:
        _dt = clock.tick(FPS) / 1000.0
        t = pygame.time.get_ticks() / 1000.0
        z = t * master_speed.value

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                break
            if event.type == pygame.VIDEORESIZE:
                w = max(MIN_WINDOW_WIDTH, event.w)
                h = max(MIN_WINDOW_HEIGHT, event.h)
                screen = pygame.display.set_mode((w, h), pygame.RESIZABLE)

            master_speed.handle_event(event)

            for idx, slot in enumerate(slots):
                if slot.type_button.handle_event(event):
                    cur = EFFECT_TYPES.index(slot.effect_type)
                    slot.effect_type = EFFECT_TYPES[(cur + 1) % len(EFFECT_TYPES)]
                    slot.type_button.label = f"Type: {slot.effect_type}"
                if slot.up_button.handle_event(event):
                    swap_slots(slots, idx, idx - 1)
                    break
                if slot.down_button.handle_event(event):
                    swap_slots(slots, idx, idx + 1)
                    break
                for s in slot.sliders.values():
                    s.handle_event(event)

        win_w, win_h = screen.get_size()
        side_rect, panel_rects, info_y = layout_ui(slots, master_speed, win_w, win_h)
        labels = stage_labels(slots)

        # Render base and stage outputs.
        stage_pixel_arrays = [pygame.PixelArray(s) for s in stage_surfaces]
        for gy in range(GRID_SIZE):
            ny = (gy / GRID_SIZE - 0.5) * NOISE_SCALE * 2.0
            for gx in range(GRID_SIZE):
                nx = (gx / GRID_SIZE - 0.5) * NOISE_SCALE * 2.0

                # Base
                val0 = fractal_noise3(noise, nx, ny, z)
                c0 = int((val0 * 0.5 + 0.5) * 255)
                col0 = (c0, c0, c0)
                stage_pixel_arrays[0][gx, gy] = col0

                x, y = nx, ny
                for si, slot in enumerate(slots, start=1):
                    if slot.effect_type == "twist":
                        x, y = apply_twist(x, y, slot.sliders)
                    elif slot.effect_type == "wobble":
                        x, y = apply_wobble(x, y, z, slot.sliders)
                    # none: passthrough

                    val = fractal_noise3(noise, x, y, z)
                    c = int((val * 0.5 + 0.5) * 255)
                    stage_pixel_arrays[si][gx, gy] = (c, c, c)
        for pa in stage_pixel_arrays:
            del pa

        screen.fill(COLOR_BG)

        # Left visualization panels
        for i, src in enumerate(stage_surfaces):
            dst_rect = panel_rects[i]
            pygame.draw.rect(screen, COLOR_PANEL_BG, dst_rect)
            pygame.draw.rect(screen, COLOR_BORDER, dst_rect, 1)
            scaled = pygame.transform.scale(src, (dst_rect.width, dst_rect.height))
            screen.blit(scaled, dst_rect.topleft)

            label = font.render(labels[i], True, COLOR_TEXT)
            screen.blit(label, (dst_rect.x + 6, dst_rect.y + 6))

        # Right control panel
        pygame.draw.rect(screen, (25, 25, 30), side_rect, border_radius=8)
        pygame.draw.rect(screen, COLOR_BORDER, side_rect, 1, border_radius=8)
        panel_title = title_font.render("Controls", True, COLOR_TEXT)
        side_left = side_rect.x
        screen.blit(panel_title, (side_left + 16, 18))

        master_speed.draw(screen, font)

        for i, slot in enumerate(slots):
            slot_y = 90 + i * 160
            title = title_font.render(f"Slot {i + 1}", True, COLOR_TEXT)
            screen.blit(title, (side_left + 326, slot_y + 3))

            slot.type_button.draw(screen, font)
            slot.up_button.draw(screen, font)
            slot.down_button.draw(screen, font)
            for sl in slot.sliders.values():
                sl.draw(screen, font)

            if slot.effect_type == "twist":
                hint = "Active params: center_x, center_y, spiral_factor, spiral_angle"
            elif slot.effect_type == "wobble":
                hint = "Active params: x_freq, y_freq, x_amp, y_amp"
            else:
                hint = "Active params: none (passthrough)"
            hint_surf = font.render(hint, True, COLOR_HINT)
            screen.blit(hint_surf, (side_left + 16, slot_y + 144))

        info = font.render("Type cycles: twist -> wobble -> none | Reorder: Up/Down", True, (150, 150, 165))
        screen.blit(info, (LEFT_MARGIN, info_y))

        pygame.display.flip()

    pygame.quit()


if __name__ == "__main__":
    main()
