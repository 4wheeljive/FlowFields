from __future__ import annotations

from dataclasses import dataclass
import math

import numpy as np
import pygame


WINDOW_WIDTH = 1260
WINDOW_HEIGHT = 920
GRID_SIZE = 64
CELL_PIXELS = 11
SIM_PIXELS = GRID_SIZE * CELL_PIXELS
SIM_X = 36
SIM_Y = 36
PANEL_X = SIM_X + SIM_PIXELS + 40
PANEL_Y = 28
PANEL_WIDTH = WINDOW_WIDTH - PANEL_X - 28

BG_COLOR = (8, 10, 16)
PANEL_COLOR = (18, 22, 32)
TEXT_COLOR = (235, 238, 245)
SUBTLE_TEXT = (150, 157, 172)
TRACK_COLOR = (45, 50, 65)
FILL_COLOR = (255, 131, 59)
KNOB_COLOR = (188, 194, 206)
BUTTON_COLOR = (38, 44, 60)
BUTTON_ACTIVE = (255, 131, 59)
GRID_BORDER = (56, 62, 82)


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


@dataclass
class Slider:
    label: str
    min_value: float
    max_value: float
    value: float
    rect: pygame.Rect
    is_int: bool = False
    dragging: bool = False

    def normalized(self) -> float:
        span = self.max_value - self.min_value
        if span <= 0.0:
            return 0.0
        return (self.value - self.min_value) / span

    def set_from_mouse(self, mouse_x: int) -> None:
        ratio = clamp((mouse_x - self.rect.left) / self.rect.width, 0.0, 1.0)
        raw = self.min_value + ratio * (self.max_value - self.min_value)
        self.value = int(round(raw)) if self.is_int else raw

    def handle_event(self, event: pygame.event.Event) -> bool:
        hitbox = self.rect.inflate(0, 10)
        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1 and hitbox.collidepoint(event.pos):
            self.dragging = True
            self.set_from_mouse(event.pos[0])
            return True
        if event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            self.dragging = False
        if event.type == pygame.MOUSEMOTION and self.dragging:
            self.set_from_mouse(event.pos[0])
            return True
        return False

    def draw(self, surface: pygame.Surface, font: pygame.font.Font, value_font: pygame.font.Font) -> None:
        pygame.draw.rect(surface, TRACK_COLOR, self.rect, border_radius=6)
        fill_width = max(0, int(self.rect.width * self.normalized()))
        if fill_width > 0:
            pygame.draw.rect(
                surface,
                FILL_COLOR,
                pygame.Rect(self.rect.left, self.rect.top, fill_width, self.rect.height),
                border_radius=6,
            )
        knob_x = self.rect.left + fill_width
        pygame.draw.circle(surface, KNOB_COLOR, (knob_x, self.rect.centery), self.rect.height // 2 + 3)

        label_surface = font.render(self.label, True, TEXT_COLOR)
        value_text = str(int(self.value)) if self.is_int else f"{self.value:.4f}"
        value_surface = value_font.render(value_text, True, TEXT_COLOR)
        surface.blit(label_surface, (self.rect.left + 8, self.rect.centery - label_surface.get_height() // 2))
        surface.blit(value_surface, (self.rect.right - value_surface.get_width() - 8, self.rect.centery - value_surface.get_height() // 2))


@dataclass
class Button:
    label: str
    rect: pygame.Rect
    toggled: bool = False

    def draw(self, surface: pygame.Surface, font: pygame.font.Font) -> None:
        color = BUTTON_ACTIVE if self.toggled else BUTTON_COLOR
        pygame.draw.rect(surface, color, self.rect, border_radius=8)
        text = font.render(self.label, True, TEXT_COLOR)
        surface.blit(text, text.get_rect(center=self.rect.center))

    def hit_test(self, pos: tuple[int, int]) -> bool:
        return self.rect.collidepoint(pos)


class FluidSolver:
    """
    Small Eulerian fluid solver for a fire-like plume.

    Per grid cell we store:
    - velocity (`u`, `v`)
    - temperature
    - smoke density

    Temperature drives buoyancy and color. Smoke carries the darker trailing
    plume. Both are transported by the same velocity field.
    """

    def __init__(self, size: int) -> None:
        self.size = size
        shape = (size + 2, size + 2)

        self.u = np.zeros(shape, dtype=np.float32)
        self.v = np.zeros(shape, dtype=np.float32)
        self.u_prev = np.zeros(shape, dtype=np.float32)
        self.v_prev = np.zeros(shape, dtype=np.float32)

        self.temperature = np.zeros(shape, dtype=np.float32)
        self.temperature_prev = np.zeros(shape, dtype=np.float32)
        self.smoke = np.zeros(shape, dtype=np.float32)
        self.smoke_prev = np.zeros(shape, dtype=np.float32)

        self._scratch = np.zeros(shape, dtype=np.float32)
        self.ii, self.jj = np.meshgrid(
            np.arange(1, size + 1, dtype=np.float32),
            np.arange(1, size + 1, dtype=np.float32),
            indexing="ij",
        )
        self.vortex_detail = 0.5

    def clear(self) -> None:
        for field in (
            self.u,
            self.v,
            self.u_prev,
            self.v_prev,
            self.temperature,
            self.temperature_prev,
            self.smoke,
            self.smoke_prev,
            self._scratch,
        ):
            field.fill(0.0)

    def add_scalar_cell(self, field: np.ndarray, x: int, y: int, amount: float) -> None:
        if 1 <= x <= self.size and 1 <= y <= self.size:
            field[x, y] += amount

    def add_velocity_cell(self, x: int, y: int, amount_x: float, amount_y: float) -> None:
        if 1 <= x <= self.size and 1 <= y <= self.size:
            self.u[x, y] += amount_x
            self.v[x, y] += amount_y

    def step(
        self,
        dt: float,
        viscosity: float,
        smoke_diffusion: float,
        temperature_diffusion: float,
        iterations: int,
        vorticity: float,
        velocity_dissipation: float,
        smoke_dissipation: float,
        temperature_dissipation: float,
    ) -> None:
        np.copyto(self.u_prev, self.u)
        np.copyto(self.v_prev, self.v)
        self.diffuse(1, self.u, self.u_prev, viscosity, dt, iterations)
        self.diffuse(2, self.v, self.v_prev, viscosity, dt, iterations)
        self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        np.copyto(self.u_prev, self.u)
        np.copyto(self.v_prev, self.v)
        self.advect(1, self.u, self.u_prev, self.u_prev, self.v_prev, dt)
        self.advect(2, self.v, self.v_prev, self.u_prev, self.v_prev, dt)
        self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        if vorticity > 0.0:
            self.apply_vorticity_confinement(dt, vorticity, self.vortex_detail)
            self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        np.copyto(self.temperature_prev, self.temperature)
        self.diffuse(0, self.temperature, self.temperature_prev, temperature_diffusion, dt, iterations)
        np.copyto(self.temperature_prev, self.temperature)
        self.advect(0, self.temperature, self.temperature_prev, self.u, self.v, dt)

        np.copyto(self.smoke_prev, self.smoke)
        self.diffuse(0, self.smoke, self.smoke_prev, smoke_diffusion, dt, iterations)
        np.copyto(self.smoke_prev, self.smoke)
        self.advect(0, self.smoke, self.smoke_prev, self.u, self.v, dt)

        if velocity_dissipation < 1.0:
            self.u[1:-1, 1:-1] *= velocity_dissipation
            self.v[1:-1, 1:-1] *= velocity_dissipation
        if smoke_dissipation < 1.0:
            self.smoke[1:-1, 1:-1] *= smoke_dissipation
        if temperature_dissipation < 1.0:
            self.temperature[1:-1, 1:-1] *= temperature_dissipation

        self.temperature[1:-1, 1:-1] = np.maximum(self.temperature[1:-1, 1:-1], 0.0)
        self.smoke[1:-1, 1:-1] = np.maximum(self.smoke[1:-1, 1:-1], 0.0)

    def set_bnd(self, b: int, x: np.ndarray) -> None:
        x[0, 1:-1] = -x[1, 1:-1] if b == 1 else x[1, 1:-1]
        x[-1, 1:-1] = -x[-2, 1:-1] if b == 1 else x[-2, 1:-1]
        x[1:-1, 0] = -x[1:-1, 1] if b == 2 else x[1:-1, 1]
        x[1:-1, -1] = -x[1:-1, -2] if b == 2 else x[1:-1, -2]
        x[0, 0] = 0.5 * (x[1, 0] + x[0, 1])
        x[0, -1] = 0.5 * (x[1, -1] + x[0, -2])
        x[-1, 0] = 0.5 * (x[-2, 0] + x[-1, 1])
        x[-1, -1] = 0.5 * (x[-2, -1] + x[-1, -2])

    def lin_solve(self, b: int, x: np.ndarray, x0: np.ndarray, a: float, c: float, iterations: int) -> None:
        inv_c = 1.0 / c
        for _ in range(iterations):
            x[1:-1, 1:-1] = (
                x0[1:-1, 1:-1]
                + a * (x[0:-2, 1:-1] + x[2:, 1:-1] + x[1:-1, 0:-2] + x[1:-1, 2:])
            ) * inv_c
            self.set_bnd(b, x)

    def diffuse(self, b: int, x: np.ndarray, x0: np.ndarray, diff: float, dt: float, iterations: int) -> None:
        a = dt * diff * self.size * self.size
        self.lin_solve(b, x, x0, a, 1.0 + 4.0 * a, iterations)

    def advect(self, b: int, d: np.ndarray, d0: np.ndarray, u: np.ndarray, v: np.ndarray, dt: float) -> None:
        dt0 = dt * self.size
        x = np.clip(self.ii - dt0 * u[1:-1, 1:-1], 0.5, self.size + 0.5)
        y = np.clip(self.jj - dt0 * v[1:-1, 1:-1], 0.5, self.size + 0.5)
        i0 = np.floor(x).astype(np.int32)
        i1 = i0 + 1
        j0 = np.floor(y).astype(np.int32)
        j1 = j0 + 1
        s1 = x - i0
        s0 = 1.0 - s1
        t1 = y - j0
        t0 = 1.0 - t1
        d[1:-1, 1:-1] = (
            s0 * (t0 * d0[i0, j0] + t1 * d0[i0, j1])
            + s1 * (t0 * d0[i1, j0] + t1 * d0[i1, j1])
        )
        self.set_bnd(b, d)

    def project(self, u: np.ndarray, v: np.ndarray, p: np.ndarray, div: np.ndarray, iterations: int) -> None:
        div[1:-1, 1:-1] = -0.5 * (
            u[2:, 1:-1] - u[0:-2, 1:-1] + v[1:-1, 2:] - v[1:-1, 0:-2]
        ) / self.size
        p.fill(0.0)
        self.set_bnd(0, div)
        self.set_bnd(0, p)
        self.lin_solve(0, p, div, 1.0, 4.0, iterations)
        u[1:-1, 1:-1] -= 0.5 * self.size * (p[2:, 1:-1] - p[0:-2, 1:-1])
        v[1:-1, 1:-1] -= 0.5 * self.size * (p[1:-1, 2:] - p[1:-1, 0:-2])
        self.set_bnd(1, u)
        self.set_bnd(2, v)

    def apply_vorticity_confinement(self, dt: float, strength: float, detail: float) -> None:
        curl = self._scratch
        curl.fill(0.0)
        curl[1:-1, 1:-1] = 0.5 * (
            self.v[2:, 1:-1] - self.v[0:-2, 1:-1] - self.u[1:-1, 2:] + self.u[1:-1, 0:-2]
        )
        magnitude = np.abs(curl)
        grad_x = np.zeros_like(curl)
        grad_y = np.zeros_like(curl)
        grad_x[1:-1, 1:-1] = 0.5 * (magnitude[2:, 1:-1] - magnitude[0:-2, 1:-1])
        grad_y[1:-1, 1:-1] = 0.5 * (magnitude[1:-1, 2:] - magnitude[1:-1, 0:-2])
        norm = np.sqrt(grad_x * grad_x + grad_y * grad_y) + 1e-6
        grad_x /= norm
        grad_y /= norm

        local_energy = magnitude[1:-1, 1:-1] / (magnitude[1:-1, 1:-1].mean() + 1e-6)
        freq = 0.25 + detail * 0.85
        phase = np.sin(self.ii * freq) * np.cos(self.jj * freq)
        local_strength = strength * (0.55 + (0.45 + 0.25 * detail) * np.clip(local_energy, 0.0, 2.0)) * (1.0 + (0.18 + 0.12 * detail) * phase)

        force_x = np.zeros_like(curl)
        force_y = np.zeros_like(curl)
        force_x[1:-1, 1:-1] = grad_y[1:-1, 1:-1] * curl[1:-1, 1:-1] * local_strength
        force_y[1:-1, 1:-1] = -grad_x[1:-1, 1:-1] * curl[1:-1, 1:-1] * local_strength
        self.u[1:-1, 1:-1] += dt * force_x[1:-1, 1:-1]
        self.v[1:-1, 1:-1] += dt * force_y[1:-1, 1:-1]
        self.set_bnd(1, self.u)
        self.set_bnd(2, self.v)


class FireApp:
    """
    Fire experiment built on the same fluid core as the smoke/logo solvers.

    The source injects temperature and smoke near the bottom center. Temperature
    creates buoyancy and drives the palette from dark smoke to orange/yellow and
    white-hot core colors.
    """

    def __init__(self) -> None:
        pygame.init()
        pygame.display.set_caption("fire1")
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        self.clock = pygame.time.Clock()
        self.font_small = pygame.font.SysFont("Menlo", 11)
        self.font_medium = pygame.font.SysFont("Menlo", 12)

        self.solver = FluidSolver(GRID_SIZE)
        self.surface = pygame.Surface((GRID_SIZE, GRID_SIZE))
        self.sim_rect = pygame.Rect(SIM_X, SIM_Y, SIM_PIXELS, SIM_PIXELS)
        self.paused = False
        self.sim_time = 0.0
        self.render_modes = ["final", "temperature", "smoke", "velocity"]
        self.render_mode_index = 0

        slider_x = PANEL_X + 16
        slider_width = PANEL_WIDTH - 32
        slider_gap = 26

        specs = [
            ("master_speed", "Master Speed", 0.05, 1.5, 0.24, False),
            ("dt", "Time Step", 0.005, 0.12, 0.065, False),
            ("source_radius", "Source Radius", 1.0, 14.0, 5.2, False),
            ("source_width", "Source Width", 0.0, 20.0, 3.4, False),
            ("source_drift", "Source Drift", 0.0, 12.0, 0.35, False),
            ("source_temp", "Source Temp", 20.0, 500.0, 445.0, False),
            ("source_smoke", "Source Smoke", 0.0, 300.0, 62.0, False),
            ("source_upward", "Source Upward", 0.0, 6.0, 4.2, False),
            ("source_jitter", "Source Jitter", 0.0, 4.0, 0.70, False),
            ("buoyancy", "Buoyancy", 0.0, 10.0, 6.2, False),
            ("cooling", "Cooling", 0.90, 1.0, 0.9972, False),
            ("smoke_from_cooling", "Smoke From Cooling", 0.0, 12.0, 2.2, False),
            ("viscosity", "Viscosity", 0.0, 0.01, 0.0002, False),
            ("smoke_diffusion", "Smoke Diffusion", 0.0, 0.02, 0.0024, False),
            ("temp_diffusion", "Temp Diffusion", 0.0, 0.02, 0.0011, False),
            ("velocity_dissipation", "Velocity Fade", 0.90, 1.0, 0.9990, False),
            ("smoke_dissipation", "Smoke Fade", 0.90, 1.0, 0.9964, False),
            ("temp_dissipation", "Temp Fade", 0.90, 1.0, 0.9935, False),
            ("iterations", "Solver Iterations", 4, 40, 24, True),
            ("vorticity", "Vorticity", 0.0, 10.0, 6.2, False),
            ("vortex_detail", "Vortex Detail", 0.0, 1.5, 0.85, False),
            ("flame_height", "Flame Height", 0.2, 3.0, 1.45, False),
            ("glow", "Glow", 0.0, 3.0, 1.45, False),
            ("sparkle", "Sparkle", 0.0, 2.0, 0.18, False),
            ("substeps", "Substeps / Frame", 1, 6, 2, True),
        ]

        self.slider_order = [key for key, *_ in specs]
        self.sliders: dict[str, Slider] = {}
        for index, (key, label, low, high, value, is_int) in enumerate(specs):
            top = PANEL_Y + 72 + index * slider_gap
            self.sliders[key] = Slider(
                label=label,
                min_value=low,
                max_value=high,
                value=value,
                rect=pygame.Rect(slider_x, top, slider_width, 14),
                is_int=is_int,
            )

        self.presets: dict[str, dict[str, float | int]] = {
            "candle": {
                "master_speed": 0.22,
                "dt": 0.055,
                "source_radius": 3.2,
                "source_width": 1.2,
                "source_drift": 0.15,
                "source_temp": 420.0,
                "source_smoke": 22.0,
                "source_upward": 4.4,
                "source_jitter": 0.28,
                "buoyancy": 6.6,
                "cooling": 0.9979,
                "smoke_from_cooling": 1.0,
                "smoke_diffusion": 0.0018,
                "temp_diffusion": 0.0008,
                "velocity_dissipation": 0.9992,
                "smoke_dissipation": 0.9978,
                "temp_dissipation": 0.9948,
                "iterations": 26,
                "vorticity": 4.8,
                "vortex_detail": 0.55,
                "flame_height": 1.35,
                "glow": 1.10,
                "sparkle": 0.03,
                "substeps": 2,
            },
            "jet": {
                "master_speed": 0.30,
                "dt": 0.060,
                "source_radius": 4.0,
                "source_width": 2.0,
                "source_drift": 0.0,
                "source_temp": 500.0,
                "source_smoke": 12.0,
                "source_upward": 5.8,
                "source_jitter": 0.10,
                "buoyancy": 7.6,
                "cooling": 0.9984,
                "smoke_from_cooling": 0.6,
                "smoke_diffusion": 0.0014,
                "temp_diffusion": 0.0007,
                "velocity_dissipation": 0.9994,
                "smoke_dissipation": 0.9982,
                "temp_dissipation": 0.9956,
                "iterations": 28,
                "vorticity": 3.8,
                "vortex_detail": 0.40,
                "flame_height": 1.55,
                "glow": 1.35,
                "sparkle": 0.00,
                "substeps": 3,
            },
            "campfire": {
                "master_speed": 0.34,
                "dt": 0.072,
                "source_radius": 8.0,
                "source_width": 8.5,
                "source_drift": 1.4,
                "source_temp": 460.0,
                "source_smoke": 150.0,
                "source_upward": 3.7,
                "source_jitter": 1.6,
                "buoyancy": 5.0,
                "cooling": 0.9958,
                "smoke_from_cooling": 5.2,
                "smoke_diffusion": 0.0046,
                "temp_diffusion": 0.0019,
                "velocity_dissipation": 0.9986,
                "smoke_dissipation": 0.9946,
                "temp_dissipation": 0.9920,
                "iterations": 22,
                "vorticity": 7.8,
                "vortex_detail": 1.15,
                "flame_height": 1.65,
                "glow": 1.70,
                "sparkle": 0.35,
                "substeps": 2,
            },
            "hellfire": {
                "master_speed": 0.32,
                "dt": 0.065,
                "source_radius": 6.5,
                "source_width": 6.0,
                "source_drift": 6.0,
                "source_temp": 420.0,
                "source_smoke": 115.0,
                "source_upward": 3.6,
                "source_jitter": 1.2,
                "buoyancy": 5.8,
                "cooling": 0.9960,
                "smoke_from_cooling": 4.8,
                "viscosity": 0.0002,
                "smoke_diffusion": 0.0035,
                "temp_diffusion": 0.0016,
                "velocity_dissipation": 0.9988,
                "smoke_dissipation": 0.9950,
                "temp_dissipation": 0.9915,
                "iterations": 22,
                "vorticity": 7.2,
                "vortex_detail": 1.0,
                "flame_height": 2.0,
                "glow": 2.4,
                "sparkle": 0.55,
                "substeps": 1,
            },
        }
        self.active_preset = "candle"

        button_y = PANEL_Y + 14
        self.pause_button = Button("Pause", pygame.Rect(PANEL_X + 16, button_y, 68, 32), toggled=False)
        self.reset_button = Button("Reset", pygame.Rect(PANEL_X + 90, button_y, 68, 32), toggled=False)
        self.candle_button = Button("Candle", pygame.Rect(PANEL_X + 164, button_y, 68, 32), toggled=True)
        self.jet_button = Button("Jet", pygame.Rect(PANEL_X + 238, button_y, 56, 32), toggled=False)
        self.campfire_button = Button("Camp", pygame.Rect(PANEL_X + 300, button_y, 62, 32), toggled=False)
        self.hellfire_button = Button("Hellfire", pygame.Rect(PANEL_X + 368, button_y, 82, 32), toggled=False)
        self.apply_preset(self.active_preset)

    def run(self) -> None:
        running = True
        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        running = False
                    elif event.key == pygame.K_SPACE:
                        self.paused = not self.paused
                        self.pause_button.toggled = self.paused
                    elif event.key == pygame.K_r:
                        self.solver.clear()
                    elif event.key == pygame.K_TAB:
                        self.render_mode_index = (self.render_mode_index + 1) % len(self.render_modes)
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    if self.pause_button.hit_test(event.pos):
                        self.paused = not self.paused
                        self.pause_button.toggled = self.paused
                    elif self.reset_button.hit_test(event.pos):
                        self.solver.clear()
                    elif self.candle_button.hit_test(event.pos):
                        self.apply_preset("candle")
                    elif self.jet_button.hit_test(event.pos):
                        self.apply_preset("jet")
                    elif self.campfire_button.hit_test(event.pos):
                        self.apply_preset("campfire")
                    elif self.hellfire_button.hit_test(event.pos):
                        self.apply_preset("hellfire")

                for slider in self.sliders.values():
                    slider.handle_event(event)

            self.update()
            self.draw()
            pygame.display.flip()
            self.clock.tick(0)

        pygame.quit()

    def update(self) -> None:
        if self.paused:
            return

        substeps = int(self.sliders["substeps"].value)
        dt = (self.sliders["dt"].value * self.sliders["master_speed"].value) / max(1, substeps)
        self.sim_time += dt * substeps
        self.solver.vortex_detail = self.sliders["vortex_detail"].value

        for _ in range(substeps):
            self.emit_fire_source(dt)
            self.apply_buoyancy(dt)
            self.solver.step(
                dt=dt,
                viscosity=self.sliders["viscosity"].value,
                smoke_diffusion=self.sliders["smoke_diffusion"].value,
                temperature_diffusion=self.sliders["temp_diffusion"].value,
                iterations=int(self.sliders["iterations"].value),
                vorticity=self.sliders["vorticity"].value,
                velocity_dissipation=self.sliders["velocity_dissipation"].value,
                smoke_dissipation=self.sliders["smoke_dissipation"].value,
                temperature_dissipation=self.sliders["temp_dissipation"].value,
            )
            self.cool_and_smoke(dt)

    def apply_preset(self, preset_name: str) -> None:
        """
        Load one preset into the live slider set.

        Presets are only value snapshots. After applying one, all sliders remain
        editable as usual; the preset simply provides a fast starting point.
        """
        preset = self.presets[preset_name]
        for key, value in preset.items():
            if key in self.sliders:
                self.sliders[key].value = int(value) if self.sliders[key].is_int else float(value)
        self.active_preset = preset_name
        self.candle_button.toggled = preset_name == "candle"
        self.jet_button.toggled = preset_name == "jet"
        self.campfire_button.toggled = preset_name == "campfire"
        self.hellfire_button.toggled = preset_name == "hellfire"

    def emit_fire_source(self, dt: float) -> None:
        """
        Inject heat and smoke near the bottom center.

        The source is spread over several exact cells so it feels like a burner
        strip instead of a single point. A gentle oscillation and jitter stop the
        flame from looking too mechanically symmetric.
        """
        radius = self.sliders["source_radius"].value
        base_temp = self.sliders["source_temp"].value * dt
        base_smoke = self.sliders["source_smoke"].value * dt
        upward = self.sliders["source_upward"].value * dt * 1.7
        jitter = self.sliders["source_jitter"].value * dt
        width = self.sliders["source_width"].value
        drift = self.sliders["source_drift"].value

        center_row = GRID_SIZE - 5
        center_col = GRID_SIZE * 0.5 + math.sin(self.sim_time * 1.9) * drift

        sample_count = 7
        for idx in range(sample_count):
            offset = (idx / max(1, sample_count - 1)) * 2.0 - 1.0
            row = int(round(clamp(center_row + abs(offset) * 0.4, 1.0, GRID_SIZE)))
            col = int(round(clamp(center_col + offset * radius * 0.9, 1.0, GRID_SIZE)))

            pulse = 0.84 + 0.16 * math.sin(self.sim_time * 7.0 + idx * 0.9)
            temp_amount = base_temp * pulse
            smoke_amount = base_smoke * (0.8 + 0.2 * pulse)

            self.solver.add_scalar_cell(self.solver.temperature, row, col, temp_amount)
            self.solver.add_scalar_cell(self.solver.smoke, row, col, smoke_amount)
            self.solver.add_velocity_cell(
                row,
                col,
                -upward * (0.85 + 0.15 * pulse),
                math.sin(self.sim_time * 9.0 + idx * 1.7) * jitter,
            )

    def apply_buoyancy(self, dt: float) -> None:
        """
        Convert heat into upward motion.

        Hotter cells rise more strongly. A little smoke contribution keeps the
        plume coherent even after the hottest core has already cooled.
        """
        buoyancy = self.sliders["buoyancy"].value
        if buoyancy <= 0.0:
            return

        temp = self.solver.temperature[1:-1, 1:-1]
        smoke = self.solver.smoke[1:-1, 1:-1]
        self.solver.u[1:-1, 1:-1] += -dt * buoyancy * ((temp * 0.018) + (smoke * 0.0022))
        self.solver.set_bnd(1, self.solver.u)

    def cool_and_smoke(self, dt: float) -> None:
        """
        Cool hot gas and convert part of that lost heat into darker smoke.

        This is the minimal extra mechanism that helps the plume transition from
        white-yellow flame core into darker orange and then gray smoke.
        """
        cooling = self.sliders["cooling"].value
        smoke_from_cooling = self.sliders["smoke_from_cooling"].value
        if cooling < 1.0:
            prev = self.solver.temperature[1:-1, 1:-1].copy()
            self.solver.temperature[1:-1, 1:-1] *= cooling
            lost_heat = np.maximum(prev - self.solver.temperature[1:-1, 1:-1], 0.0)
            if smoke_from_cooling > 0.0:
                self.solver.smoke[1:-1, 1:-1] += lost_heat * smoke_from_cooling * dt * 0.35

    def draw(self) -> None:
        """
        Convert temperature and smoke into a stylized fire palette.

        Rendering is palette-driven rather than literal RGB transport:
        - smoke darkens and desaturates
        - low heat becomes red/orange
        - high heat becomes yellow/white
        - velocity adds a small emissive boost
        """
        self.screen.fill(BG_COLOR)

        temp = np.clip(self.solver.temperature[1:-1, 1:-1], 0.0, 255.0)
        smoke = np.clip(self.solver.smoke[1:-1, 1:-1], 0.0, 255.0)
        velocity_mag = np.sqrt(
            self.solver.u[1:-1, 1:-1] * self.solver.u[1:-1, 1:-1]
            + self.solver.v[1:-1, 1:-1] * self.solver.v[1:-1, 1:-1]
        )
        render_mode = self.render_modes[self.render_mode_index]

        if render_mode == "temperature":
            # Raw temperature view. If this is black, the problem is before the
            # fire palette: emission, buoyancy, cooling or solver transport.
            temp_vis = np.clip(temp * 8.0, 0.0, 255.0).astype(np.uint8)
            rgb = np.dstack((temp_vis, temp_vis, temp_vis))
        elif render_mode == "smoke":
            # Raw smoke view. This tells us whether smoke is being emitted and
            # produced from cooling independently of the final fire colors.
            smoke_vis = np.clip(smoke * 18.0, 0.0, 255.0).astype(np.uint8)
            rgb = np.dstack((smoke_vis, smoke_vis, smoke_vis))
        elif render_mode == "velocity":
            # Velocity magnitude view. Useful to verify that motion exists even
            # if the scalar fields are not yet visible enough.
            vel_vis = np.clip(velocity_mag * 280.0, 0.0, 255.0).astype(np.uint8)
            rgb = np.dstack((vel_vis, vel_vis, vel_vis))
        else:
            rgb = self.render_fire_palette(temp, smoke, velocity_mag)

        pygame.surfarray.blit_array(self.surface, np.transpose(rgb, (1, 0, 2)))
        scaled = pygame.transform.scale(self.surface, (SIM_PIXELS, SIM_PIXELS))
        self.screen.blit(scaled, (SIM_X, SIM_Y))
        pygame.draw.rect(self.screen, GRID_BORDER, self.sim_rect, width=2, border_radius=4)

        panel_rect = pygame.Rect(PANEL_X, PANEL_Y, PANEL_WIDTH, WINDOW_HEIGHT - PANEL_Y - 24)
        pygame.draw.rect(self.screen, PANEL_COLOR, panel_rect, border_radius=14)
        self.pause_button.draw(self.screen, self.font_medium)
        self.reset_button.draw(self.screen, self.font_medium)
        self.candle_button.draw(self.screen, self.font_medium)
        self.jet_button.draw(self.screen, self.font_medium)
        self.campfire_button.draw(self.screen, self.font_medium)
        self.hellfire_button.draw(self.screen, self.font_medium)
        for key in self.slider_order:
            self.sliders[key].draw(self.screen, self.font_medium, self.font_small)

        fps_text = self.font_medium.render(f"FPS: {self.clock.get_fps():5.1f}", True, TEXT_COLOR)
        self.screen.blit(fps_text, (SIM_X, self.sim_rect.bottom + 12))
        stats_text = self.font_small.render(
            f"Mode:{render_mode}  Tmax:{float(temp.max()):5.2f}  Smax:{float(smoke.max()):5.2f}  Vmax:{float(velocity_mag.max()):5.2f}  Tab:cycle",
            True,
            SUBTLE_TEXT,
        )
        self.screen.blit(stats_text, (SIM_X + 92, self.sim_rect.bottom + 16))

    def render_fire_palette(self, temp: np.ndarray, smoke: np.ndarray, velocity_mag: np.ndarray) -> np.ndarray:
        """
        Final fire look used in normal mode.

        Keeping this as a separate function makes debugging easier because the
        diagnostic render modes can bypass the palette entirely.
        """
        flame_height = self.sliders["flame_height"].value
        glow = self.sliders["glow"].value
        sparkle = self.sliders["sparkle"].value

        # Hellfire is meant to reproduce the original fire1 look exactly. The
        # visible difference was not caused by the preset slider values, but by
        # the later palette tuning added in fire2. We therefore keep a dedicated
        # branch here that uses the same normalization and color ramps as fire1.
        if self.active_preset == "hellfire":
            temp_scale = max(8.0, float(temp.max()) * 0.55)
            smoke_scale = max(4.0, float(smoke.max()) * 0.65)
            vel_scale = max(0.35, float(velocity_mag.max()) * 0.75)

            heat = np.clip((temp / temp_scale) * flame_height, 0.0, 1.0)
            smoke_n = np.clip(smoke / smoke_scale, 0.0, 1.0)
            speed_glow = np.clip((velocity_mag / vel_scale) * 0.40 * glow, 0.0, 1.0)

            r = np.clip(heat * 2.8 + smoke_n * 0.24, 0.0, 1.0)
            g = np.clip((heat - 0.16) * 2.2, 0.0, 1.0)
            b = np.clip((heat - 0.72) * 3.6, 0.0, 1.0)

            hot_core = np.clip((heat - 0.78) * 4.5, 0.0, 1.0)
            r = np.clip(r + hot_core * 0.35, 0.0, 1.0)
            g = np.clip(g + hot_core * 0.45, 0.0, 1.0)
            b = np.clip(b + hot_core * 0.70, 0.0, 1.0)

            cooling_mask = np.clip(smoke_n * (1.0 - heat * 0.75), 0.0, 1.0)
            gray = smoke_n * 0.20
            r = np.clip(r * (1.0 - cooling_mask * 0.48) + gray, 0.0, 1.0)
            g = np.clip(g * (1.0 - cooling_mask * 0.62) + gray, 0.0, 1.0)
            b = np.clip(b * (1.0 - cooling_mask * 0.82) + gray * 0.8, 0.0, 1.0)

            if sparkle > 0.0:
                sparkle_field = (
                    np.sin(self.solver.ii * 0.37 + self.sim_time * 17.0)
                    * np.cos(self.solver.jj * 0.29 + self.sim_time * 11.0)
                )
                sparkle_field = np.clip((sparkle_field * 0.5 + 0.5) * sparkle, 0.0, 1.0)
                hot_mask = np.clip((heat - 0.62) * 2.2, 0.0, 1.0)
                r = np.clip(r + sparkle_field * hot_mask * 0.12, 0.0, 1.0)
                g = np.clip(g + sparkle_field * hot_mask * 0.10, 0.0, 1.0)

            brightness = np.clip(0.18 + heat * 1.35 + speed_glow, 0.0, 1.55)
            rgb = np.dstack((r, g, b)) * brightness[..., None]
            return np.clip(rgb * 255.0, 0.0, 255.0).astype(np.uint8)

        # The other presets keep the calmer fire2 palette tuning.
        temp_scale = max(10.0, float(temp.max()) * 0.78)
        smoke_scale = max(6.0, float(smoke.max()) * 0.95)
        vel_scale = max(0.45, float(velocity_mag.max()) * 0.95)

        heat = np.clip((temp / temp_scale) * flame_height, 0.0, 1.0)
        smoke_n = np.clip(smoke / smoke_scale, 0.0, 1.0)
        speed_glow = np.clip((velocity_mag / vel_scale) * 0.22 * glow, 0.0, 1.0)

        r = np.clip(heat * 2.8 + smoke_n * 0.24, 0.0, 1.0)
        g = np.clip((heat - 0.16) * 2.2, 0.0, 1.0)
        b = np.clip((heat - 0.72) * 3.6, 0.0, 1.0)

        hot_core = np.clip((heat - 0.84) * 5.4, 0.0, 1.0)
        r = np.clip(r + hot_core * 0.24, 0.0, 1.0)
        g = np.clip(g + hot_core * 0.34, 0.0, 1.0)
        b = np.clip(b + hot_core * 0.54, 0.0, 1.0)

        cooling_mask = np.clip(smoke_n * (1.0 - heat * 0.75), 0.0, 1.0)
        gray = smoke_n * 0.16
        r = np.clip(r * (1.0 - cooling_mask * 0.44) + gray, 0.0, 1.0)
        g = np.clip(g * (1.0 - cooling_mask * 0.58) + gray, 0.0, 1.0)
        b = np.clip(b * (1.0 - cooling_mask * 0.80) + gray * 0.72, 0.0, 1.0)

        if sparkle > 0.0:
            sparkle_field = (
                np.sin(self.solver.ii * 0.37 + self.sim_time * 17.0)
                * np.cos(self.solver.jj * 0.29 + self.sim_time * 11.0)
            )
            sparkle_field = np.clip((sparkle_field * 0.5 + 0.5) * sparkle, 0.0, 1.0)
            hot_mask = np.clip((heat - 0.72) * 2.0, 0.0, 1.0)
            r = np.clip(r + sparkle_field * hot_mask * 0.08, 0.0, 1.0)
            g = np.clip(g + sparkle_field * hot_mask * 0.06, 0.0, 1.0)

        brightness = np.clip(0.10 + heat * 1.08 + speed_glow, 0.0, 1.30)
        rgb = np.dstack((r, g, b)) * brightness[..., None]
        return np.clip(rgb * 255.0, 0.0, 255.0).astype(np.uint8)


def main() -> None:
    FireApp().run()


if __name__ == "__main__":
    main()