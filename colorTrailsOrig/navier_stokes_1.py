from __future__ import annotations

import colorsys
from dataclasses import dataclass
import math

import numpy as np
import pygame


# Window and layout constants.
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

# Simple UI palette.
BG_COLOR = (8, 10, 16)
PANEL_COLOR = (18, 22, 32)
TEXT_COLOR = (235, 238, 245)
SUBTLE_TEXT = (150, 157, 172)
TRACK_COLOR = (45, 50, 65)
FILL_COLOR = (74, 163, 255)
KNOB_COLOR = (225, 236, 255)
BUTTON_COLOR = (38, 44, 60)
BUTTON_ACTIVE = (74, 163, 255)
GRID_BORDER = (56, 62, 82)


def clamp(value: float, low: float, high: float) -> float:
    """Clamp a scalar into a closed interval."""
    return max(low, min(high, value))


@dataclass
class Slider:
    """Minimal runtime slider for a single simulation parameter."""

    label: str
    min_value: float
    max_value: float
    value: float
    rect: pygame.Rect
    is_int: bool = False
    dragging: bool = False

    def normalized(self) -> float:
        """Return the current value mapped to [0, 1] for drawing."""
        span = self.max_value - self.min_value
        if span <= 0.0:
            return 0.0
        return (self.value - self.min_value) / span

    def set_from_mouse(self, mouse_x: int) -> None:
        """Translate mouse x-position into a parameter value."""
        ratio = clamp((mouse_x - self.rect.left) / self.rect.width, 0.0, 1.0)
        raw = self.min_value + ratio * (self.max_value - self.min_value)
        self.value = int(round(raw)) if self.is_int else raw

    def handle_event(self, event: pygame.event.Event) -> bool:
        """Update dragging state and slider value from mouse input."""
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

    def draw(self, surface: pygame.Surface, font: pygame.font.Font, value_font: pygame.font.Font) -> None:
        """Draw the slider track, fill, knob, label and numeric value."""
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
        value_surface = value_font.render(value_text, True, SUBTLE_TEXT)
        surface.blit(label_surface, (self.rect.left, self.rect.top - 20))
        surface.blit(value_surface, (self.rect.right - value_surface.get_width(), self.rect.top - 20))


@dataclass
class Button:
    """Simple clickable button used for pause/reset."""

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
    Grid-based 2D fluid solver.

    The simulation is Eulerian: we keep state on a fixed 64x64 grid instead of
    tracking particles. Each cell stores:
    - two velocity components (u, v)
    - three dye channels (r, g, b)
    Temporary arrays exist because diffusion/advection are solved in-place by
    repeatedly reading the previous state and writing the new state.
    """

    def __init__(self, size: int) -> None:
        self.size = size
        shape = (size + 2, size + 2)

        # Velocity field. Two components are needed because flow has direction.
        self.u = np.zeros(shape, dtype=np.float32)
        self.v = np.zeros(shape, dtype=np.float32)

        # Previous velocity state used as input during solver passes.
        self.u_prev = np.zeros(shape, dtype=np.float32)
        self.v_prev = np.zeros(shape, dtype=np.float32)

        # Dye is stored as three scalar fields so color is transported by the
        # fluid exactly the same way as density would be.
        self.dye_r = np.zeros(shape, dtype=np.float32)
        self.dye_g = np.zeros(shape, dtype=np.float32)
        self.dye_b = np.zeros(shape, dtype=np.float32)
        self.dye_r_prev = np.zeros(shape, dtype=np.float32)
        self.dye_g_prev = np.zeros(shape, dtype=np.float32)
        self.dye_b_prev = np.zeros(shape, dtype=np.float32)

        # Shared scratch buffer for intermediate calculations such as curl.
        self._scratch = np.zeros(shape, dtype=np.float32)

        # Precomputed interior coordinates avoid rebuilding the same index grid
        # every frame during semi-Lagrangian advection.
        self.ii, self.jj = np.meshgrid(
            np.arange(1, size + 1, dtype=np.float32),
            np.arange(1, size + 1, dtype=np.float32),
            indexing="ij",
        )

    def clear(self) -> None:
        """Reset the entire simulation state."""
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
        ):
            field.fill(0.0)

    def add_dye(self, x: int, y: int, color: tuple[float, float, float], amount: float, radius: float) -> None:
        """Inject colored dye by splatting each color channel into the grid."""
        self._splat(self.dye_r, x, y, color[0] * amount, radius)
        self._splat(self.dye_g, x, y, color[1] * amount, radius)
        self._splat(self.dye_b, x, y, color[2] * amount, radius)

    def add_velocity(self, x: int, y: int, amount_x: float, amount_y: float, radius: float) -> None:
        """Inject momentum into both velocity components."""
        self._splat(self.u, x, y, amount_x, radius)
        self._splat(self.v, x, y, amount_y, radius)

    def _splat(self, field: np.ndarray, x: int, y: int, amount: float, radius: float) -> None:
        """
        Add a smooth localized contribution.

        A Gaussian-like weight is used so the emitter does not affect a single
        hard cell only. That makes the source look softer and reduces aliasing.
        """
        if radius <= 0.0:
            return

        r = max(1, int(np.ceil(radius)))
        x0 = max(1, x - r)
        x1 = min(self.size, x + r)
        y0 = max(1, y - r)
        y1 = min(self.size, y + r)
        if x0 > x1 or y0 > y1:
            return

        xs = np.arange(x0, x1 + 1, dtype=np.float32)
        ys = np.arange(y0, y1 + 1, dtype=np.float32)
        grid_x, grid_y = np.meshgrid(xs, ys, indexing="ij")
        dist2 = (grid_x - x) ** 2 + (grid_y - y) ** 2
        weights = np.exp(-dist2 / max(1e-5, radius * radius * 0.6)).astype(np.float32)
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
    ) -> None:
        """
        Advance the simulation by one time step.

        The sequence is the classic stable-fluids pattern:
        1. diffuse velocity
        2. project to make it approximately divergence-free
        3. advect velocity by itself
        4. project again
        5. optionally add vorticity confinement
        6. diffuse and advect dye through the new velocity field
        7. apply simple dissipation to keep the system bounded
        """
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
            self.apply_vorticity_confinement(dt, vorticity)
            self.project(self.u, self.v, self.u_prev, self.v_prev, iterations)

        for dye, dye_prev in (
            (self.dye_r, self.dye_r_prev),
            (self.dye_g, self.dye_g_prev),
            (self.dye_b, self.dye_b_prev),
        ):
            np.copyto(dye_prev, dye)
            self.diffuse(0, dye, dye_prev, diffusion, dt, iterations)
            np.copyto(dye_prev, dye)
            self.advect(0, dye, dye_prev, self.u, self.v, dt)

        if velocity_dissipation < 1.0:
            self.u[1:-1, 1:-1] *= velocity_dissipation
            self.v[1:-1, 1:-1] *= velocity_dissipation
        if dye_dissipation < 1.0:
            self.dye_r[1:-1, 1:-1] *= dye_dissipation
            self.dye_g[1:-1, 1:-1] *= dye_dissipation
            self.dye_b[1:-1, 1:-1] *= dye_dissipation

        self.dye_r[1:-1, 1:-1] = np.maximum(self.dye_r[1:-1, 1:-1], 0.0)
        self.dye_g[1:-1, 1:-1] = np.maximum(self.dye_g[1:-1, 1:-1], 0.0)
        self.dye_b[1:-1, 1:-1] = np.maximum(self.dye_b[1:-1, 1:-1], 0.0)

    def set_bnd(self, b: int, x: np.ndarray) -> None:
        """
        Apply boundary conditions.

        Scalar fields mirror at the border. Velocity components flip sign at
        the boundary that corresponds to their outward normal.
        """
        x[0, 1:-1] = -x[1, 1:-1] if b == 1 else x[1, 1:-1]
        x[-1, 1:-1] = -x[-2, 1:-1] if b == 1 else x[-2, 1:-1]
        x[1:-1, 0] = -x[1:-1, 1] if b == 2 else x[1:-1, 1]
        x[1:-1, -1] = -x[1:-1, -2] if b == 2 else x[1:-1, -2]
        x[0, 0] = 0.5 * (x[1, 0] + x[0, 1])
        x[0, -1] = 0.5 * (x[1, -1] + x[0, -2])
        x[-1, 0] = 0.5 * (x[-2, 0] + x[-1, 1])
        x[-1, -1] = 0.5 * (x[-2, -1] + x[-1, -2])

    def lin_solve(self, b: int, x: np.ndarray, x0: np.ndarray, a: float, c: float, iterations: int) -> None:
        """
        Iterative relaxation solve for diffusion/pressure.

        This is the numerically cheap part of the stable-fluids approach: we
        trade exactness for stability and interactive speed.
        """
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
        """
        Move a field through the velocity field by backtracing.

        For each current cell, we ask: from where did this parcel come one time
        step ago? We sample the old field there via bilinear interpolation.
        """
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
        """
        Make the velocity field approximately incompressible.

        Without this step, the flow would visually expand/contract in a way
        that does not look like fluid. We estimate divergence, solve for a
        pressure-like field, then subtract its gradient.
        """
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
        """
        Re-inject small-scale swirl.

        Stable solvers are visually smooth but can damp fine vortices too much.
        This term restores some curl so the flow looks livelier.
        """
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
        force_x = grad_y * curl * strength
        force_y = -grad_x * curl * strength
        self.u[1:-1, 1:-1] += dt * force_x[1:-1, 1:-1]
        self.v[1:-1, 1:-1] += dt * force_y[1:-1, 1:-1]
        self.set_bnd(1, self.u)
        self.set_bnd(2, self.v)


class FluidApp:
    """Application wrapper: UI, emitter animation, rendering and main loop."""

    def __init__(self) -> None:
        pygame.init()
        pygame.display.set_caption("solver3")
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        self.clock = pygame.time.Clock()
        self.font_small = pygame.font.SysFont("Menlo", 13)
        self.font_medium = pygame.font.SysFont("Menlo", 15)

        self.solver = FluidSolver(GRID_SIZE)
        self.surface = pygame.Surface((GRID_SIZE, GRID_SIZE))
        self.sim_rect = pygame.Rect(SIM_X, SIM_Y, SIM_PIXELS, SIM_PIXELS)
        self.paused = False
        self.sim_time = 0.0

        slider_x = PANEL_X + 18
        slider_width = PANEL_WIDTH - 36
        slider_gap = 40

        # Keep only parameters that directly affect solver behavior or emitter
        # behavior. These are the knobs needed to understand and tune the demo.
        specs = [
            ("master_speed", "Master Speed", 0.05, 1.0, 0.0938, False),
            ("dt", "Time Step", 0.01, 0.25, 0.0397, False),
            ("viscosity", "Viscosity", 0.0, 0.01, 0.0, False),
            ("diffusion", "Dye Diffusion", 0.0, 0.01, 0.0, False),
            ("velocity_dissipation", "Velocity Fade", 0.90, 1.0, 0.9985, False),
            ("dye_dissipation", "Dye Fade", 0.90, 1.0, 0.9983, False),
            ("iterations", "Solver Iterations", 4, 40, 5, True),
            ("vorticity", "Vorticity", 0.0, 8.0, 6.8738, False),
            ("emitter_density", "Emitter Density", 0.0, 120.0, 120.0, False),
            ("emitter_force", "Emitter Force", 0.0, 180.0, 0.8738, False),
            ("emitter_radius", "Emitter Radius", 1.0, 12.0, 12.0, False),
            ("emitter_spread", "Emitter Spread", 0.0, 60.0, 0.0, False),
            ("emitter_angle", "Emitter Angle", -85.0, 85.0, 0.0, False),
            ("emitter_swing_amp", "Swing Amplitude", 0.0, 85.0, 33.2160, False),
            ("emitter_swing_speed", "Swing Speed", 0.0, 1.5, 0.2767, False),
            ("gravity", "Gravity", -60.0, 60.0, 2.9126, False),
            ("emitter_hue_speed", "Emitter Hue Speed", 0.0, 1.5, 0.6917, False),
            ("substeps", "Substeps / Frame", 1, 6, 1, True),
        ]

        self.slider_order = [key for key, *_ in specs]
        self.sliders: dict[str, Slider] = {}
        for index, (key, label, low, high, value, is_int) in enumerate(specs):
            top = PANEL_Y + 88 + index * slider_gap
            self.sliders[key] = Slider(
                label=label,
                min_value=low,
                max_value=high,
                value=value,
                rect=pygame.Rect(slider_x, top, slider_width, 10),
                is_int=is_int,
            )

        button_y = PANEL_Y + 18
        self.pause_button = Button("Pause", pygame.Rect(PANEL_X + 18, button_y, 100, 34), toggled=False)
        self.reset_button = Button("Reset", pygame.Rect(PANEL_X + 130, button_y, 100, 34), toggled=False)

    def run(self) -> None:
        """Main event/update/render loop. Uncapped to run as fast as possible."""
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
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    if self.pause_button.hit_test(event.pos):
                        self.paused = not self.paused
                        self.pause_button.toggled = self.paused
                    elif self.reset_button.hit_test(event.pos):
                        self.solver.clear()

                for slider in self.sliders.values():
                    slider.handle_event(event)

            self.update()
            self.draw()
            pygame.display.flip()
            self.clock.tick(0)

        pygame.quit()

    def update(self) -> None:
        """Advance emitter animation and fluid simulation."""
        if self.paused:
            return

        substeps = int(self.sliders["substeps"].value)
        dt = (self.sliders["dt"].value * self.sliders["master_speed"].value) / max(1, substeps)
        self.sim_time += dt * substeps

        # The emitter stays at a fixed position. Only the emission angle swings
        # over time, which produces a periodic change in outflow direction.
        animated_angle = self.sliders["emitter_angle"].value + math.sin(
            self.sim_time * self.sliders["emitter_swing_speed"].value * math.tau
        ) * self.sliders["emitter_swing_amp"].value
        emitter_color = colorsys.hsv_to_rgb(
            (self.sim_time * self.sliders["emitter_hue_speed"].value) % 1.0,
            0.8,
            1.0,
        )

        for _ in range(substeps):
            self.emit_stationary_source(
                dt=dt,
                color=emitter_color,
                density=self.sliders["emitter_density"].value,
                force=self.sliders["emitter_force"].value,
                radius=self.sliders["emitter_radius"].value,
                spread=self.sliders["emitter_spread"].value,
                angle_degrees=animated_angle,
            )
            self.apply_gravity(dt, self.sliders["gravity"].value)
            self.solver.step(
                dt=dt,
                viscosity=self.sliders["viscosity"].value,
                diffusion=self.sliders["diffusion"].value,
                iterations=int(self.sliders["iterations"].value),
                vorticity=self.sliders["vorticity"].value,
                velocity_dissipation=self.sliders["velocity_dissipation"].value,
                dye_dissipation=self.sliders["dye_dissipation"].value,
            )

    def emit_stationary_source(
        self,
        dt: float,
        color: tuple[float, float, float],
        density: float,
        force: float,
        radius: float,
        spread: float,
        angle_degrees: float,
    ) -> None:
        """
        Inject dye and momentum near the lower center of the grid.

        The emitter is built from multiple overlapping layers so it looks like a
        continuous plume instead of a single pixel impulse.
        """
        row = GRID_SIZE - 10
        col = int(round(GRID_SIZE * 0.5 + 1.0))
        amount_scale = dt * 4.0
        angle = math.radians(angle_degrees)

        # Angle 0 means straight up. Positive angles rotate the jet to the
        # right, negative angles to the left.
        vertical = -math.cos(angle) * force * amount_scale
        horizontal = math.sin(angle) * force * amount_scale

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
            self.solver.add_velocity(sx, sy, vertical * velocity_weight, horizontal * velocity_weight, sr)

        # Optional side injections widen the plume by pushing outwards while
        # still carrying some upward motion.
        if spread > 0.0:
            side_offset = max(2, int(round(radius * 0.9)))
            side_radius = max(1.0, radius * 0.95)
            lateral = spread * amount_scale * 0.35
            side_vertical = vertical * 0.18
            side_horizontal = horizontal * 0.18
            self.solver.add_velocity(
                row,
                int(clamp(col - side_offset, 1.0, GRID_SIZE)),
                side_vertical,
                side_horizontal - lateral,
                side_radius,
            )
            self.solver.add_velocity(
                row,
                int(clamp(col + side_offset, 1.0, GRID_SIZE)),
                side_vertical,
                side_horizontal + lateral,
                side_radius,
            )

    def apply_gravity(self, dt: float, gravity: float) -> None:
        """
        Apply a uniform vertical force to the whole velocity field.

        Positive gravity pushes the fluid downward, negative gravity upward.
        This is a simple external force term added before the solver step.
        """
        if gravity == 0.0:
            return

        self.solver.u[1:-1, 1:-1] += gravity * dt * 0.35
        self.solver.set_bnd(1, self.solver.u)

    def draw(self) -> None:
        """
        Render the dye fields as RGB.

        Velocity magnitude is added back in as a soft glow so fast-moving regions
        visually read as more energetic without needing extra overlays.
        """
        self.screen.fill(BG_COLOR)

        dye_r = np.clip(self.solver.dye_r[1:-1, 1:-1], 0.0, 255.0)
        dye_g = np.clip(self.solver.dye_g[1:-1, 1:-1], 0.0, 255.0)
        dye_b = np.clip(self.solver.dye_b[1:-1, 1:-1], 0.0, 255.0)
        velocity_mag = np.sqrt(
            self.solver.u[1:-1, 1:-1] * self.solver.u[1:-1, 1:-1]
            + self.solver.v[1:-1, 1:-1] * self.solver.v[1:-1, 1:-1]
        )
        # Use velocity primarily as a brightness boost, not a neutral additive
        # glow. That keeps hues saturated instead of washing them toward gray.
        brightness = 1.0 + np.clip(velocity_mag * 0.006, 0.0, 0.22)

        rgb_float = np.dstack((dye_r, dye_g, dye_b)) * brightness[..., None]

        # Re-boost chroma after transport. Diffusion and mixing naturally pull
        # channels toward their average; pushing colors away from the local mean
        # helps preserve vivid hues for longer.
        mean = rgb_float.mean(axis=2, keepdims=True)
        rgb_float = mean + (rgb_float - mean) * 1.35

        red = np.clip(rgb_float[:, :, 0], 0.0, 255.0).astype(np.uint8)
        green = np.clip(rgb_float[:, :, 1], 0.0, 255.0).astype(np.uint8)
        blue = np.clip(rgb_float[:, :, 2], 0.0, 255.0).astype(np.uint8)
        rgb = np.dstack((red, green, blue))

        pygame.surfarray.blit_array(self.surface, np.transpose(rgb, (1, 0, 2)))
        scaled = pygame.transform.scale(self.surface, (SIM_PIXELS, SIM_PIXELS))
        self.screen.blit(scaled, (SIM_X, SIM_Y))
        pygame.draw.rect(self.screen, GRID_BORDER, self.sim_rect, width=2, border_radius=4)

        panel_rect = pygame.Rect(PANEL_X, PANEL_Y, PANEL_WIDTH, WINDOW_HEIGHT - PANEL_Y - 32)
        pygame.draw.rect(self.screen, PANEL_COLOR, panel_rect, border_radius=14)
        self.pause_button.draw(self.screen, self.font_medium)
        self.reset_button.draw(self.screen, self.font_medium)
        for key in self.slider_order:
            self.sliders[key].draw(self.screen, self.font_medium, self.font_small)

        fps_text = self.font_medium.render(f"FPS: {self.clock.get_fps():5.1f}", True, TEXT_COLOR)
        self.screen.blit(fps_text, (SIM_X, self.sim_rect.bottom + 12))


def main() -> None:
    FluidApp().run()


if __name__ == "__main__":
    main()