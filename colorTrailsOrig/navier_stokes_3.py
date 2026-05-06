from __future__ import annotations

"""Stefan Petrick, 2026.

Interactive 2D fluid study with a moving paddle obstacle and stylized rendering.
The solver is inspired by Jos Stam's "Stable Fluids" paper and serves as a
prototype for a FastLED fluid simulation.

This file is a real-time fluid simulation sketch for LED-based visuals, built
as a semi-Lagrangian grid solver with a deliberately lightweight numerical core.
That keeps the simulation responsive and visually expressive in real time.

Artistic direction and visual choices by Stefan Petrick.
Released under the MIT License.
"""

import colorsys
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
PANEL_Y = 40
PANEL_WIDTH = WINDOW_WIDTH - PANEL_X - 32

BG_COLOR = (8, 10, 16)
PANEL_COLOR = (18, 22, 32)
TEXT_COLOR = (235, 238, 245)
SUBTLE_TEXT = (150, 157, 172)
TRACK_COLOR = (26, 45, 86)
FILL_COLOR = (90, 44, 148)
KNOB_COLOR = (116, 56, 186)
BUTTON_COLOR = (34, 82, 168)
BUTTON_ACTIVE = (62, 126, 227)
GRID_BORDER = (56, 62, 82)
OBSTACLE_COLOR = (205, 218, 245)
OBSTACLE_OUTLINE = (88, 112, 168)
OBSTACLE_WIDTH = 10
OBSTACLE_THICKNESS = 2


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
    label_surface: pygame.Surface | None = None
    label_rect: pygame.Rect | None = None

    def normalized(self) -> float:
        span = self.max_value - self.min_value
        if span <= 0.0:
            return 0.0
        return (self.value - self.min_value) / span

    def set_from_mouse(self, mouse_x: int) -> None:
        ratio = clamp((mouse_x - self.rect.left) / self.rect.width, 0.0, 1.0)
        value = self.min_value + ratio * (self.max_value - self.min_value)
        self.value = int(round(value)) if self.is_int else value

    def handle_event(self, event: pygame.event.Event) -> bool:
        hitbox = self.rect.inflate(0, 12)
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

    def prepare_label(self, font: pygame.font.Font) -> None:
        self.label_surface = font.render(self.label, True, TEXT_COLOR)
        self.label_rect = self.label_surface.get_rect(midleft=(self.rect.left + 12, self.rect.centery))

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
        knob_radius = max(5, self.rect.height // 2 + 1)
        pygame.draw.circle(surface, KNOB_COLOR, (knob_x, self.rect.centery), knob_radius)

        value_text = str(int(self.value)) if self.is_int else f"{self.value:.4f}"
        value_surface = value_font.render(value_text, True, TEXT_COLOR)
        value_rect = value_surface.get_rect(midright=(self.rect.right - 12, self.rect.centery))
        if self.label_surface is None or self.label_rect is None:
            self.prepare_label(font)
        surface.blit(self.label_surface, self.label_rect)
        surface.blit(value_surface, value_rect)


@dataclass
class Button:
    label: str
    rect: pygame.Rect
    toggled: bool = False
    text_surface: pygame.Surface | None = None
    text_rect: pygame.Rect | None = None

    def prepare_label(self, font: pygame.font.Font) -> None:
        self.text_surface = font.render(self.label, True, TEXT_COLOR)
        self.text_rect = self.text_surface.get_rect(center=self.rect.center)

    def draw(self, surface: pygame.Surface, font: pygame.font.Font) -> None:
        color = BUTTON_ACTIVE if self.toggled else BUTTON_COLOR
        pygame.draw.rect(surface, color, self.rect, border_radius=8)
        if self.text_surface is None or self.text_rect is None:
            self.prepare_label(font)
        surface.blit(self.text_surface, self.text_rect)

    def hit_test(self, pos: tuple[int, int]) -> bool:
        return self.rect.collidepoint(pos)


class FluidSolver:
    def __init__(self, size: int) -> None:
        self.size = size
        shape = (size + 2, size + 2)
        inner_shape = (size, size)
        # The extra one-cell border on each side makes boundary handling simpler.
        # All simulation work happens in [1:-1, 1:-1].
        self.u = np.zeros(shape, dtype=np.float32)
        self.v = np.zeros(shape, dtype=np.float32)
        self.u_prev = np.zeros(shape, dtype=np.float32)
        self.v_prev = np.zeros(shape, dtype=np.float32)
        self.dye_r = np.zeros(shape, dtype=np.float32)
        self.dye_g = np.zeros(shape, dtype=np.float32)
        self.dye_b = np.zeros(shape, dtype=np.float32)
        self.dye_r_prev = np.zeros(shape, dtype=np.float32)
        self.dye_g_prev = np.zeros(shape, dtype=np.float32)
        self.dye_b_prev = np.zeros(shape, dtype=np.float32)
        self._scratch = np.zeros(shape, dtype=np.float32)
        self._grad_x = np.zeros(shape, dtype=np.float32)
        self._grad_y = np.zeros(shape, dtype=np.float32)
        self._splat_cache: dict[int, np.ndarray] = {}
        self.obstacle_mask = np.zeros(shape, dtype=bool)
        # This soft mask is only there to take the hard edge off the inner
        # paddle corners around the gap. The gap itself stays open.
        self.obstacle_soft_mask = np.zeros(shape, dtype=np.float32)
        self.obstacle_bounds = (1, 1, 1, 1)
        self.obstacle_segments: list[tuple[int, int, int, int]] = []
        self.has_obstacle = False
        # These arrays are reused every frame to keep advection cheap.
        self._advect_x = np.zeros(inner_shape, dtype=np.float32)
        self._advect_y = np.zeros(inner_shape, dtype=np.float32)
        self._advect_i0 = np.zeros(inner_shape, dtype=np.int32)
        self._advect_i1 = np.zeros(inner_shape, dtype=np.int32)
        self._advect_j0 = np.zeros(inner_shape, dtype=np.int32)
        self._advect_j1 = np.zeros(inner_shape, dtype=np.int32)
        self._advect_s0 = np.zeros(inner_shape, dtype=np.float32)
        self._advect_s1 = np.zeros(inner_shape, dtype=np.float32)
        self._advect_t0 = np.zeros(inner_shape, dtype=np.float32)
        self._advect_t1 = np.zeros(inner_shape, dtype=np.float32)
        self.ii, self.jj = np.meshgrid(
            np.arange(1, size + 1, dtype=np.float32),
            np.arange(1, size + 1, dtype=np.float32),
            indexing="ij",
        )
        self.fast_diffusion_threshold = 1e-8

    def clear(self) -> None:
        # Resetting in-place avoids reallocating a bunch of large arrays.
        for field in (
            self.u,
            self.v,
            self.u_prev,
            self.v_prev,
            self.dye_r,
            self.dye_g,
            self.dye_b,
            self.dye_r_prev,
            self.dye_g_prev,
            self.dye_b_prev,
            self._scratch,
            self._grad_x,
            self._grad_y,
        ):
            field.fill(0.0)
        self.obstacle_mask.fill(False)
        self.obstacle_soft_mask.fill(0.0)
        self.obstacle_segments = []
        self.has_obstacle = False

    def add_dye(self, x: int, y: int, color: tuple[float, float, float], amount: float, radius: float) -> None:
        self._splat(self.dye_r, x, y, color[0] * amount, radius)
        self._splat(self.dye_g, x, y, color[1] * amount, radius)
        self._splat(self.dye_b, x, y, color[2] * amount, radius)

    def add_velocity(self, x: int, y: int, amount_x: float, amount_y: float, radius: float) -> None:
        self._splat(self.u, x, y, amount_x, radius)
        self._splat(self.v, x, y, amount_y, radius)

    def _splat(self, field: np.ndarray, x: int, y: int, amount: float, radius: float) -> None:
        if radius <= 0.0:
            return
        r = max(1, int(np.ceil(radius)))
        x0 = max(1, x - r)
        x1 = min(self.size, x + r)
        y0 = max(1, y - r)
        y1 = min(self.size, y + r)
        if x0 > x1 or y0 > y1:
            return

        dist2 = self._splat_cache.get(r)
        if dist2 is None:
            # Radius values repeat a lot while dragging sliders, so caching the
            # radial falloff avoids rebuilding the same kernel over and over.
            offsets = np.arange(-r, r + 1, dtype=np.float32)
            dx, dy = np.meshgrid(offsets, offsets, indexing="ij")
            dist2 = dx * dx + dy * dy
            self._splat_cache[r] = dist2

        kernel_x0 = x0 - (x - r)
        kernel_x1 = kernel_x0 + (x1 - x0) + 1
        kernel_y0 = y0 - (y - r)
        kernel_y1 = kernel_y0 + (y1 - y0) + 1
        weights = np.exp(
            -dist2[kernel_x0:kernel_x1, kernel_y0:kernel_y1] / max(1e-5, radius * radius * 0.6),
            dtype=np.float32,
        )
        field[x0 : x1 + 1, y0 : y1 + 1] += amount * weights

    def step(
        self,
        dt: float,
        viscosity: float,
        diffusion: float,
        iterations: int,
        vorticity: float,
        velocity_dissipation: float,
        dye_dissipation: float,
        gravity_u: float,
        gravity_v: float,
    ) -> None:
        # Velocity first: diffuse if needed, add global force, project to keep
        # things roughly incompressible, then advect forward.
        viscosity_a = dt * viscosity * self.size * self.size
        if viscosity_a > self.fast_diffusion_threshold:
            np.copyto(self.u_prev, self.u)
            np.copyto(self.v_prev, self.v)
            self.diffuse(1, self.u, self.u_prev, viscosity_a, iterations)
            self.diffuse(2, self.v, self.v_prev, viscosity_a, iterations)
        if gravity_u != 0.0 or gravity_v != 0.0:
            self.apply_global_force(dt, gravity_u, gravity_v)
        self.apply_obstacle_boundary()
        self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        np.copyto(self.u_prev, self.u)
        np.copyto(self.v_prev, self.v)

        self.advect(1, self.u, self.u_prev, self.u_prev, self.v_prev, dt)
        self.advect(2, self.v, self.v_prev, self.u_prev, self.v_prev, dt)
        self.apply_obstacle_boundary()
        self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        if vorticity > 0.0:
            # Vorticity confinement is the cheap "give it more swirl" pass.
            self.apply_vorticity_confinement(dt, vorticity)
            self.apply_obstacle_boundary()
            self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        diffusion_a = dt * diffusion * self.size * self.size
        # Dye is handled after velocity so the color simply rides on the latest flow.
        for dye, dye_prev in (
            (self.dye_r, self.dye_r_prev),
            (self.dye_g, self.dye_g_prev),
            (self.dye_b, self.dye_b_prev),
        ):
            if diffusion_a > self.fast_diffusion_threshold:
                np.copyto(dye_prev, dye)
                self.diffuse(0, dye, dye_prev, diffusion_a, iterations)
            np.copyto(dye_prev, dye)
            self.advect(0, dye, dye_prev, self.u, self.v, dt)
            if self.has_obstacle:
                # Solid obstacle cells are always empty. That keeps the paddle
                # shape crisp even after advection tries to smear through it.
                for row0, row1, col0, col1 in self.obstacle_segments:
                    dye[row0 : row1 + 1, col0 : col1 + 1] = 0.0

        if velocity_dissipation < 1.0:
            self.u[1:-1, 1:-1] *= velocity_dissipation
            self.v[1:-1, 1:-1] *= velocity_dissipation
        if dye_dissipation < 1.0:
            self.dye_r[1:-1, 1:-1] *= dye_dissipation
            self.dye_g[1:-1, 1:-1] *= dye_dissipation
            self.dye_b[1:-1, 1:-1] *= dye_dissipation

        np.maximum(self.dye_r[1:-1, 1:-1], 0.0, out=self.dye_r[1:-1, 1:-1])
        np.maximum(self.dye_g[1:-1, 1:-1], 0.0, out=self.dye_g[1:-1, 1:-1])
        np.maximum(self.dye_b[1:-1, 1:-1], 0.0, out=self.dye_b[1:-1, 1:-1])
        self.apply_obstacle_boundary()

    def set_bnd(self, b: int, x: np.ndarray) -> None:
        # b selects which component is mirrored at the wall:
        # 0 = scalar field, 1 = horizontal velocity, 2 = vertical velocity.
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
        # Classic Gauss-Seidel relaxation. Not fancy, but very predictable and fast enough here.
        for _ in range(iterations):
            x[1:-1, 1:-1] = (
                x0[1:-1, 1:-1]
                + a
                * (
                    x[0:-2, 1:-1]
                    + x[2:, 1:-1]
                    + x[1:-1, 0:-2]
                    + x[1:-1, 2:]
                )
            ) * inv_c
            self.set_bnd(b, x)

    def diffuse(self, b: int, x: np.ndarray, x0: np.ndarray, a: float, iterations: int) -> None:
        if a <= self.fast_diffusion_threshold:
            np.copyto(x, x0)
            self.set_bnd(b, x)
            return
        self.lin_solve(b, x, x0, a, 1.0 + 4.0 * a, iterations)

    def apply_global_force(self, dt: float, force_u: float, force_v: float) -> None:
        # Gravity is just a constant force added everywhere in the domain.
        self.u[1:-1, 1:-1] += dt * force_u
        self.v[1:-1, 1:-1] += dt * force_v
        self.set_bnd(1, self.u)
        self.set_bnd(2, self.v)

    def set_obstacle(self, row_center: int, col_center: int, width: int, thickness: int) -> None:
        # The paddle is stored as two solid blocks with an open slot in the middle.
        # That makes the geometry easy to reason about in both the solver and renderer.
        half_width = width // 2
        half_thickness = thickness // 2
        row0 = int(clamp(row_center - half_thickness, 1, self.size))
        row1 = int(clamp(row_center + max(0, thickness - half_thickness - 1), 1, self.size))
        col0 = int(clamp(col_center - half_width, 1, self.size))
        col1 = int(clamp(col_center + max(0, width - half_width - 1), 1, self.size))
        self.obstacle_mask.fill(False)
        self.obstacle_soft_mask.fill(0.0)
        gap_width = max(1, int(round(width / 3)))
        solid_total = max(0, width - gap_width)
        left_width = solid_total // 2
        right_width = solid_total - left_width
        gap_start = col0 + left_width
        gap_end = min(col1, gap_start + gap_width - 1)
        self.obstacle_segments = []
        if left_width > 0:
            left_col1 = min(col1, gap_start - 1)
            self.obstacle_mask[row0 : row1 + 1, col0 : left_col1 + 1] = True
            self.obstacle_segments.append((row0, row1, col0, left_col1))
        if right_width > 0:
            right_col0 = max(col0, gap_end + 1)
            self.obstacle_mask[row0 : row1 + 1, right_col0 : col1 + 1] = True
            self.obstacle_segments.append((row0, row1, right_col0, col1))
        inner_row0 = max(1, row0 - 1)
        inner_row1 = min(self.size, row1 + 1)
        if left_width > 0:
            # These soft cells sit on the solid side of the gap edge. They do
            # not block the slot itself; they just reduce the visual harshness.
            inner_left_col = gap_start - 1
            self.obstacle_soft_mask[row0 : row1 + 1, inner_left_col] = np.maximum(
                self.obstacle_soft_mask[row0 : row1 + 1, inner_left_col],
                0.18,
            )
            self.obstacle_soft_mask[inner_row0 : inner_row1 + 1, inner_left_col] = np.maximum(
                self.obstacle_soft_mask[inner_row0 : inner_row1 + 1, inner_left_col],
                0.22,
            )
        if right_width > 0:
            inner_right_col = gap_end + 1
            self.obstacle_soft_mask[row0 : row1 + 1, inner_right_col] = np.maximum(
                self.obstacle_soft_mask[row0 : row1 + 1, inner_right_col],
                0.18,
            )
            self.obstacle_soft_mask[inner_row0 : inner_row1 + 1, inner_right_col] = np.maximum(
                self.obstacle_soft_mask[inner_row0 : inner_row1 + 1, inner_right_col],
                0.22,
            )
        self.obstacle_bounds = (row0, row1, col0, col1)
        self.has_obstacle = bool(self.obstacle_segments)
        self.apply_obstacle_boundary()

    def apply_obstacle_boundary(self) -> None:
        if not self.has_obstacle:
            return

        # First apply the hard stop inside the solid paddle segments.
        for row0, row1, col0, col1 in self.obstacle_segments:
            segment_mask = self.obstacle_mask[row0 : row1 + 1, col0 : col1 + 1]
            self.u[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.v[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.dye_r[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.dye_g[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.dye_b[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.u_prev[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.v_prev[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.dye_r_prev[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.dye_g_prev[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0
            self.dye_b_prev[row0 : row1 + 1, col0 : col1 + 1][segment_mask] = 0.0

            top = max(1, row0 - 1)
            bottom = min(self.size, row1 + 1)
            left = max(1, col0 - 1)
            right = min(self.size, col1 + 1)

            self.u[top, col0 : col1 + 1] = np.minimum(self.u[top, col0 : col1 + 1], 0.0)
            self.u[bottom, col0 : col1 + 1] = np.maximum(self.u[bottom, col0 : col1 + 1], 0.0)
            self.v[row0 : row1 + 1, left] = np.minimum(self.v[row0 : row1 + 1, left], 0.0)
            self.v[row0 : row1 + 1, right] = np.maximum(self.v[row0 : row1 + 1, right], 0.0)
        row0, row1, col0, col1 = self.obstacle_bounds
        soft = self.obstacle_soft_mask[row0 : row1 + 1, col0 : col1 + 1]
        if np.any(soft > 0.0):
            # Then blend just a little near the inner corners so the flow
            # through the gap does not look unnaturally pixel-perfect.
            inv_soft = 1.0 - soft
            dye_fade = 1.0 - soft * 0.35
            self.u[row0 : row1 + 1, col0 : col1 + 1] *= inv_soft
            self.v[row0 : row1 + 1, col0 : col1 + 1] *= inv_soft
            self.u_prev[row0 : row1 + 1, col0 : col1 + 1] *= inv_soft
            self.v_prev[row0 : row1 + 1, col0 : col1 + 1] *= inv_soft
            self.dye_r[row0 : row1 + 1, col0 : col1 + 1] *= dye_fade
            self.dye_g[row0 : row1 + 1, col0 : col1 + 1] *= dye_fade
            self.dye_b[row0 : row1 + 1, col0 : col1 + 1] *= dye_fade
            self.dye_r_prev[row0 : row1 + 1, col0 : col1 + 1] *= dye_fade
            self.dye_g_prev[row0 : row1 + 1, col0 : col1 + 1] *= dye_fade
            self.dye_b_prev[row0 : row1 + 1, col0 : col1 + 1] *= dye_fade
        self.set_bnd(1, self.u)
        self.set_bnd(2, self.v)

    def advect(self, b: int, d: np.ndarray, d0: np.ndarray, u: np.ndarray, v: np.ndarray, dt: float) -> None:
        dt0 = dt * self.size
        # Semi-Lagrangian backtrace: ask "where did this cell come from?" and
        # sample the previous field there. Stable, forgiving, and perfect for a toy solver.
        x = self._advect_x
        y = self._advect_y
        np.subtract(self.ii, dt0 * u[1:-1, 1:-1], out=x)
        np.subtract(self.jj, dt0 * v[1:-1, 1:-1], out=y)
        np.clip(x, 0.5, self.size + 0.5, out=x)
        np.clip(y, 0.5, self.size + 0.5, out=y)

        i0 = self._advect_i0
        i1 = self._advect_i1
        j0 = self._advect_j0
        j1 = self._advect_j1
        np.floor(x, out=x)
        i0[:, :] = x.astype(np.int32)
        i1[:, :] = i0 + 1
        np.subtract(self.ii, dt0 * u[1:-1, 1:-1], out=self._advect_s1)
        np.clip(self._advect_s1, 0.5, self.size + 0.5, out=self._advect_s1)
        np.subtract(self._advect_s1, i0, out=self._advect_s1)
        np.floor(y, out=y)
        j0[:, :] = y.astype(np.int32)
        j1[:, :] = j0 + 1
        np.subtract(self.jj, dt0 * v[1:-1, 1:-1], out=self._advect_t1)
        np.clip(self._advect_t1, 0.5, self.size + 0.5, out=self._advect_t1)
        np.subtract(self._advect_t1, j0, out=self._advect_t1)

        s1 = self._advect_s1
        s0 = self._advect_s0
        t1 = self._advect_t1
        t0 = self._advect_t0
        np.subtract(1.0, s1, out=s0)
        np.subtract(1.0, t1, out=t0)
        d[1:-1, 1:-1] = (
            s0 * (t0 * d0[i0, j0] + t1 * d0[i0, j1])
            + s1 * (t0 * d0[i1, j0] + t1 * d0[i1, j1])
        )
        self.set_bnd(b, d)

    def project(
        self,
        u: np.ndarray,
        v: np.ndarray,
        p: np.ndarray,
        div: np.ndarray,
        iterations: int,
    ) -> None:
        # Projection removes divergence so the velocity behaves more like an
        # incompressible fluid instead of a field that just expands everywhere.
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

    def apply_vorticity_confinement(self, dt: float, strength: float) -> None:
        # This is a stylized force that pushes energy back into curled areas so
        # the smoke keeps some bite instead of going flat too quickly.
        curl = self._scratch
        curl.fill(0.0)
        curl[1:-1, 1:-1] = 0.5 * (
            self.v[2:, 1:-1] - self.v[0:-2, 1:-1] - self.u[1:-1, 2:] + self.u[1:-1, 0:-2]
        )

        magnitude = np.abs(curl)
        grad_x = self._grad_x
        grad_y = self._grad_y
        grad_x.fill(0.0)
        grad_y.fill(0.0)
        grad_x[1:-1, 1:-1] = 0.5 * (magnitude[2:, 1:-1] - magnitude[0:-2, 1:-1])
        grad_y[1:-1, 1:-1] = 0.5 * (magnitude[1:-1, 2:] - magnitude[1:-1, 0:-2])

        norm = np.sqrt(grad_x * grad_x + grad_y * grad_y) + 1e-6
        grad_x /= norm
        grad_y /= norm

        force_x = grad_y * curl * strength
        force_y = -grad_x * curl * strength
        self.u[1:-1, 1:-1] += dt * force_x[1:-1, 1:-1]
        self.v[1:-1, 1:-1] += dt * force_y[1:-1, 1:-1]
        self.set_bnd(1, self.u)
        self.set_bnd(2, self.v)


class FluidApp:
    def __init__(self) -> None:
        pygame.init()
        pygame.display.set_caption("solver1")
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        self.clock = pygame.time.Clock()
        self.font_small = pygame.font.SysFont("Menlo", 14)
        self.font_medium = pygame.font.SysFont("Menlo", 16)
        self.font_large = pygame.font.SysFont("Menlo", 20)

        self.solver = FluidSolver(GRID_SIZE)
        self.surface = pygame.Surface((GRID_SIZE, GRID_SIZE))
        self.sim_rect = pygame.Rect(SIM_X, SIM_Y, SIM_PIXELS, SIM_PIXELS)
        # These buffers let the render path stay fully array-based without
        # churning through new allocations every frame.
        self.rgb_buffer = np.empty((GRID_SIZE, GRID_SIZE, 3), dtype=np.uint8)
        self.velocity_mag_buffer = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)
        self.color_buffer_r = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)
        self.color_buffer_g = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)
        self.color_buffer_b = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)
        self.glow_buffer_r = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)
        self.glow_buffer_g = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)
        self.glow_buffer_b = np.empty((GRID_SIZE, GRID_SIZE), dtype=np.float32)

        self.paused = False
        self.sim_time = 0.0
        # These are mirrors of slider values that the render path reads every frame.
        self.color_contrast = 1.18
        self.flow_saturation = 0.38
        self.flow_brightness = 0.18
        self.glow_strength = 0.24
        self.highlight_saturation = 0.22
        self.black_level = 0.035

        slider_x = PANEL_X + 18
        slider_width = PANEL_WIDTH - 36
        slider_height = 20
        slider_gap = 34

        specs = [
            ("dt", "Time Step", 0.0, 0.01, 0.0039, False),
            ("viscosity", "Viscosity", 0.0, 0.01, 0.0002, False),
            ("diffusion", "Dye Diffusion", 0.0, 0.01, 0.0002, False),
            ("gravity_intensity", "Gravity Intensity", 0.0, 1.0, 1.0, False),
            ("gravity_angle", "Gravity Angle", 0.0, 360.0, 90.0, False),
            ("velocity_dissipation", "Velocity Fade", 0.90, 1.0, 0.997, False),
            ("dye_dissipation", "Dye Fade", 0.90, 1.0, 0.9995, False),
            ("color_contrast", "Color Contrast", 0.8, 1.8, 0.8000, False),
            ("black_level", "Black Point", 0.0, 0.12, 0.0897, False),
            ("flow_saturation", "Flow Saturation", 0.0, 1.0, 0.5583, False),
            ("flow_brightness", "Flow Brightness", 0.0, 0.5, 0.18, False),
            ("iterations", "Solver Iterations", 4, 40, 23, True),
            ("vorticity", "Vorticity", 0.0, 12.0, 12.0, False),
            ("emitter_density", "Emitter Density", 0.0, 120.0, 117.6699, False),
            ("emitter_upward", "Emitter Upward", 0.0, 1.0, 0.8956, False),
            ("emitter_radius", "Emitter Radius", 1.0, 12.0, 12.0, False),
            ("emitter_swing_amp", "Emitter Swing Amp", 0.0, 18.0, 18.0, False),
            ("emitter_swing_speed", "Emitter Swing Speed", 0.0, 1.5, 0.1056, False),
            ("emitter_hue_speed", "Emitter Hue Speed", 0.0, 1.5, 0.2439, False),
            ("obstacle_width", "Obstacle Width", 1, 64, 26, True),
            ("obstacle_speed", "Obstacle Speed", 0.0, 1.5, 0.0437, False),
        ]
        self.slider_order = [key for key, *_ in specs]
        self.sliders: dict[str, Slider] = {}
        for index, (key, label, low, high, value, is_int) in enumerate(specs):
            top = PANEL_Y + 82 + index * slider_gap
            self.sliders[key] = Slider(
                label=label,
                min_value=low,
                max_value=high,
                value=value,
                rect=pygame.Rect(slider_x, top, slider_width, slider_height),
                is_int=is_int,
            )
            self.sliders[key].prepare_label(self.font_medium)

        button_y = PANEL_Y + 18
        self.pause_button = Button("Pause", pygame.Rect(PANEL_X + 18, button_y, 100, 34), toggled=False)
        self.reset_button = Button("Reset", pygame.Rect(PANEL_X + 130, button_y, 100, 34), toggled=False)
        self.pause_button.prepare_label(self.font_medium)
        self.reset_button.prepare_label(self.font_medium)
        self.obstacle_row = GRID_SIZE // 2
        self.obstacle_col = GRID_SIZE // 2
        # Build the initial obstacle immediately so the solver starts in a valid state.
        self.update_obstacle(0.0)

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
                        self.update_obstacle(self.sim_time)
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    if self.pause_button.hit_test(event.pos):
                        self.paused = not self.paused
                        self.pause_button.toggled = self.paused
                    elif self.reset_button.hit_test(event.pos):
                        self.solver.clear()
                        self.update_obstacle(self.sim_time)

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

        # Substeps are fixed at 1 here. Keeping this constant makes timing and
        # tuning easier to reason about while the rest of the controls stay live.
        dt = self.sliders["dt"].value
        viscosity = self.sliders["viscosity"].value
        diffusion = self.sliders["diffusion"].value
        gravity_intensity = self.sliders["gravity_intensity"].value
        gravity_angle = self.sliders["gravity_angle"].value
        iterations = int(self.sliders["iterations"].value)
        vorticity = self.sliders["vorticity"].value
        velocity_dissipation = self.sliders["velocity_dissipation"].value
        dye_dissipation = self.sliders["dye_dissipation"].value
        color_contrast = self.sliders["color_contrast"].value
        black_level = self.sliders["black_level"].value
        flow_saturation = self.sliders["flow_saturation"].value
        flow_brightness = self.sliders["flow_brightness"].value
        emitter_density = self.sliders["emitter_density"].value
        emitter_upward = self.sliders["emitter_upward"].value
        emitter_radius = self.sliders["emitter_radius"].value
        emitter_swing_amp = self.sliders["emitter_swing_amp"].value
        emitter_swing_speed = self.sliders["emitter_swing_speed"].value
        emitter_hue_speed = self.sliders["emitter_hue_speed"].value
        obstacle_width = int(self.sliders["obstacle_width"].value)
        obstacle_speed = self.sliders["obstacle_speed"].value
        gravity_radians = math.radians(gravity_angle)
        # Screen-space convention:
        # 0° = right, 90° = down, 180° = left, 270° = up.
        gravity_u = math.sin(gravity_radians) * gravity_intensity
        gravity_v = math.cos(gravity_radians) * gravity_intensity
        self.sim_time += dt
        animated_offset = math.sin(self.sim_time * emitter_swing_speed * math.tau) * emitter_swing_amp
        emitter_color = colorsys.hsv_to_rgb((self.sim_time * emitter_hue_speed) % 1.0, 1.0, 1.0)
        self.color_contrast = color_contrast
        self.black_level = black_level
        self.flow_saturation = flow_saturation
        self.flow_brightness = flow_brightness
        self.update_obstacle(self.sim_time * obstacle_speed, obstacle_width)

        self.emit_stationary_source(
            dt=dt,
            color=emitter_color,
            density=emitter_density,
            upward=emitter_upward,
            radius=emitter_radius,
            offset_x=animated_offset,
        )
        self.solver.step(
            dt=dt,
            viscosity=viscosity,
            diffusion=diffusion,
            iterations=iterations,
            vorticity=vorticity,
            velocity_dissipation=velocity_dissipation,
            dye_dissipation=dye_dissipation,
            gravity_u=gravity_u,
            gravity_v=gravity_v,
        )

    def update_obstacle(self, obstacle_time: float, obstacle_width: int = OBSTACLE_WIDTH) -> None:
        obstacle_width = int(clamp(obstacle_width, 1, GRID_SIZE))
        # The paddle swings around the middle, but stays far enough from the
        # outer walls that the hole remains readable.
        travel = max(0, (GRID_SIZE - obstacle_width - 6) // 2)
        center_col = GRID_SIZE // 2 + int(round(math.sin(obstacle_time * math.tau) * travel))
        min_center = 1 + (obstacle_width - 1) / 2
        max_center = GRID_SIZE - (obstacle_width - 1) / 2
        self.obstacle_col = int(round(clamp(center_col, min_center, max_center)))
        self.solver.set_obstacle(
            row_center=self.obstacle_row,
            col_center=self.obstacle_col,
            width=obstacle_width,
            thickness=OBSTACLE_THICKNESS,
        )

    def emit_stationary_source(
        self,
        dt: float,
        color: tuple[float, float, float],
        density: float,
        upward: float,
        radius: float,
        offset_x: float,
    ) -> None:
        # The source is layered vertically so it feels more like a plume and
        # less like a single hard stamp each frame.
        row = GRID_SIZE - 10
        col = int(round(clamp(GRID_SIZE * 0.5 + 1.0 + offset_x, 2.0, GRID_SIZE - 1.0)))
        amount_scale = dt * 4.0
        layers = (
            (0.55, 1.00, 0.00, 0.00),
            (0.30, 0.82, 0.00, -1.20),
            (0.15, 0.65, 0.00, -2.20),
        )

        for density_weight, velocity_weight, x_shift, y_shift in layers:
            sx = int(round(clamp(row + y_shift, 1.0, GRID_SIZE)))
            sy = int(round(clamp(col + x_shift, 1.0, GRID_SIZE)))
            sr = max(1.0, radius * (1.15 - 0.1 * abs(y_shift)))
            self.solver.add_dye(sx, sy, color, density * density_weight * amount_scale, sr)
            self.solver.add_velocity(sx, sy, -upward * velocity_weight * amount_scale, 0.0, sr)

    def draw(self) -> None:
        self.screen.fill(BG_COLOR)

        # Start from the raw dye fields and a simple velocity magnitude estimate.
        dye_r = self.solver.dye_r[1:-1, 1:-1]
        dye_g = self.solver.dye_g[1:-1, 1:-1]
        dye_b = self.solver.dye_b[1:-1, 1:-1]
        velocity_mag = self.velocity_mag_buffer
        np.square(self.solver.u[1:-1, 1:-1], out=velocity_mag)
        velocity_mag += self.solver.v[1:-1, 1:-1] * self.solver.v[1:-1, 1:-1]
        np.sqrt(velocity_mag, out=velocity_mag)
        velocity_mag *= 1.8
        np.clip(velocity_mag, 0.0, 255.0, out=velocity_mag)

        red = self.color_buffer_r
        green = self.color_buffer_g
        blue = self.color_buffer_b
        np.add(dye_r, velocity_mag * 0.08, out=red)
        np.add(dye_g, velocity_mag * 0.08, out=green)
        np.add(dye_b, velocity_mag * 0.08, out=blue)
        np.clip(red, 0.0, 255.0, out=red)
        np.clip(green, 0.0, 255.0, out=green)
        np.clip(blue, 0.0, 255.0, out=blue)
        velocity_mag *= 1.0 / 255.0
        red *= 1.0 / 255.0
        green *= 1.0 / 255.0
        blue *= 1.0 / 255.0

        # Faster flow gets a little more saturation and brightness so active
        # regions read clearly even when the dye values themselves are similar.
        np.sqrt(velocity_mag, out=velocity_mag)
        luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722
        saturation_boost = 1.0 + velocity_mag * self.flow_saturation
        brightness_boost = 0.94 + velocity_mag * self.flow_brightness
        red[:] = (luminance + (red - luminance) * saturation_boost) * brightness_boost
        green[:] = (luminance + (green - luminance) * saturation_boost) * brightness_boost
        blue[:] = (luminance + (blue - luminance) * saturation_boost) * brightness_boost

        # Highlights get a second pass so the brightest pockets pop a bit more.
        np.maximum(red, green, out=velocity_mag)
        np.maximum(velocity_mag, blue, out=velocity_mag)
        velocity_mag -= 0.42
        np.clip(velocity_mag, 0.0, 1.0, out=velocity_mag)
        velocity_mag *= 1.0 / 0.58
        highlight_saturation = 1.0 + velocity_mag * self.highlight_saturation
        luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722
        red[:] = luminance + (red - luminance) * highlight_saturation
        green[:] = luminance + (green - luminance) * highlight_saturation
        blue[:] = luminance + (blue - luminance) * highlight_saturation

        # A tiny blur on just the brightest values is enough to fake a glow.
        glow_r = self.glow_buffer_r
        glow_g = self.glow_buffer_g
        glow_b = self.glow_buffer_b
        np.maximum(red - 0.55, 0.0, out=glow_r)
        np.maximum(green - 0.55, 0.0, out=glow_g)
        np.maximum(blue - 0.55, 0.0, out=glow_b)
        glow_r[1:-1, 1:-1] = (
            glow_r[1:-1, 1:-1] * 0.42
            + glow_r[0:-2, 1:-1] * 0.145
            + glow_r[2:, 1:-1] * 0.145
            + glow_r[1:-1, 0:-2] * 0.145
            + glow_r[1:-1, 2:] * 0.145
        )
        glow_g[1:-1, 1:-1] = (
            glow_g[1:-1, 1:-1] * 0.42
            + glow_g[0:-2, 1:-1] * 0.145
            + glow_g[2:, 1:-1] * 0.145
            + glow_g[1:-1, 0:-2] * 0.145
            + glow_g[1:-1, 2:] * 0.145
        )
        glow_b[1:-1, 1:-1] = (
            glow_b[1:-1, 1:-1] * 0.42
            + glow_b[0:-2, 1:-1] * 0.145
            + glow_b[2:, 1:-1] * 0.145
            + glow_b[1:-1, 0:-2] * 0.145
            + glow_b[1:-1, 2:] * 0.145
        )
        red += glow_r * self.glow_strength
        green += glow_g * self.glow_strength
        blue += glow_b * self.glow_strength

        # Black point pushes dim values down so the bright colors feel cleaner.
        red -= self.black_level
        green -= self.black_level
        blue -= self.black_level
        red *= 1.0 / (1.0 - self.black_level)
        green *= 1.0 / (1.0 - self.black_level)
        blue *= 1.0 / (1.0 - self.black_level)
        np.clip(red, 0.0, 1.0, out=red)
        np.clip(green, 0.0, 1.0, out=green)
        np.clip(blue, 0.0, 1.0, out=blue)
        gamma = 1.0 / max(0.2, self.color_contrast)

        self.rgb_buffer[:, :, 0] = (np.power(red, gamma) * 255.0).astype(np.uint8)
        self.rgb_buffer[:, :, 1] = (np.power(green, gamma) * 255.0).astype(np.uint8)
        self.rgb_buffer[:, :, 2] = (np.power(blue, gamma) * 255.0).astype(np.uint8)

        pygame.surfarray.blit_array(self.surface, np.transpose(self.rgb_buffer, (1, 0, 2)))
        scaled = pygame.transform.scale(self.surface, (SIM_PIXELS, SIM_PIXELS))
        self.screen.blit(scaled, (SIM_X, SIM_Y))
        self.draw_obstacle()
        pygame.draw.rect(self.screen, GRID_BORDER, self.sim_rect, width=2, border_radius=4)

        panel_rect = pygame.Rect(PANEL_X, PANEL_Y, PANEL_WIDTH, WINDOW_HEIGHT - PANEL_Y - 32)
        pygame.draw.rect(self.screen, PANEL_COLOR, panel_rect, border_radius=14)

        self.pause_button.draw(self.screen, self.font_medium)
        self.reset_button.draw(self.screen, self.font_medium)

        for key in self.slider_order:
            self.sliders[key].draw(self.screen, self.font_medium, self.font_small)

        fps_text = self.font_medium.render(f"FPS: {self.clock.get_fps():5.1f}", True, TEXT_COLOR)
        self.screen.blit(fps_text, (SIM_X, self.sim_rect.bottom + 12))

    def draw_obstacle(self) -> None:
        # The solver already stores the paddle as two solid segments, so drawing
        # those exact segments keeps the visuals in sync with the simulation.
        for row0, row1, col0, col1 in self.solver.obstacle_segments:
            left = SIM_X + (col0 - 1) * CELL_PIXELS
            top = SIM_Y + (row0 - 1) * CELL_PIXELS
            width = (col1 - col0 + 1) * CELL_PIXELS
            height = (row1 - row0 + 1) * CELL_PIXELS
            obstacle_rect = pygame.Rect(left, top, width, height)
            pygame.draw.rect(self.screen, OBSTACLE_COLOR, obstacle_rect, border_radius=4)
            pygame.draw.rect(self.screen, OBSTACLE_OUTLINE, obstacle_rect, width=2, border_radius=4)


def main() -> None:
    FluidApp().run()


if __name__ == "__main__":
    main()