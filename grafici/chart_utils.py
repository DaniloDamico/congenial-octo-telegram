from __future__ import annotations

import csv
import math
import os
import shutil
import subprocess
import tempfile
from html import escape
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "grafici"
FULL_MATRIX_PATH = ROOT / "logs" / "full-matrix" / "results.tsv"
PAGE_SIZES_PATH = ROOT / "logs" / "full-matrix-mmap-page-sizes" / "results.tsv"

MODEL_ORDER = ["phold", "pcs", "highway"]
MODEL_LABELS = {"phold": "PHOLD", "pcs": "PCS", "highway": "HIGHWAY"}

MODE_ORDER = [
    "grid_ckpt",
    "grid_ckpt_bs",
    "chunk_ckpt",
    "chunk_full_ckpt",
    "mmap_mv",
    "mmap_mv_store",
    "mmap_mv_store_grid",
]

MODE_LABELS = {
    "grid_ckpt": "grid",
    "grid_ckpt_bs": "grid-bs",
    "chunk_ckpt": "chunk",
    "chunk_full_ckpt": "full",
    "mmap_mv": "mmap",
    "mmap_mv_store": "mmap-store",
    "mmap_mv_store_grid": "mmap-store-grid",
}

MVM_MODE_ORDER = ["mmap_mv", "mmap_mv_store", "mmap_mv_store_grid"]

MODE_COLORS = {
    "grid_ckpt": "#2C7FB8",
    "grid_ckpt_bs": "#7FCDBB",
    "chunk_ckpt": "#F28E2B",
    "chunk_full_ckpt": "#E15759",
    "mmap_mv": "#1B9E77",
    "mmap_mv_store": "#7570B3",
    "mmap_mv_store_grid": "#D95F02",
}

AXIS_COLOR = "#3C4048"
GRID_COLOR = "#D6DCE5"
BACKGROUND = "#FAFBFD"
TEXT_COLOR = "#23272F"


def ensure_output_dir() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def to_float(value: str) -> float:
    return float(value) if value else 0.0


def to_int(value: str) -> int:
    return int(float(value)) if value else 0


def nice_upper_bound(value: float) -> float:
    if value <= 0:
        return 1.0
    magnitude = 10 ** math.floor(math.log10(value))
    normalized = value / magnitude
    if normalized <= 1:
        step = 1
    elif normalized <= 2:
        step = 2
    elif normalized <= 2.5:
        step = 2.5
    elif normalized <= 5:
        step = 5
    else:
        step = 10
    return step * magnitude


def compact_number(value: float) -> str:
    abs_value = abs(value)
    if abs_value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if abs_value >= 100_000:
        return f"{value / 1_000:.0f}k"
    if abs_value >= 1_000:
        return f"{value / 1_000:.1f}k"
    if float(value).is_integer():
        return str(int(value))
    return f"{value:.1f}"


def rgb_from_hex(color: str) -> tuple[int, int, int]:
    color = color.lstrip("#")
    return tuple(int(color[index:index + 2], 16) for index in (0, 2, 4))


def hex_from_rgb(rgb: tuple[int, int, int]) -> str:
    return "#" + "".join(f"{max(0, min(255, channel)):02X}" for channel in rgb)


def mix(color_a: str, color_b: str, ratio: float) -> str:
    r1, g1, b1 = rgb_from_hex(color_a)
    r2, g2, b2 = rgb_from_hex(color_b)
    return hex_from_rgb(
        (
            round(r1 + (r2 - r1) * ratio),
            round(g1 + (g2 - g1) * ratio),
            round(b1 + (b2 - b1) * ratio),
        )
    )


def band_centers(x: float, width: float, count: int) -> tuple[list[float], float]:
    band = width / count
    return [x + (index + 0.5) * band for index in range(count)], band


def draw_legend(
    svg: SvgDocument,
    *,
    items: list[tuple[str, str]],
    x: float,
    y: float,
    gap: float = 118,
    text_size: int = 13,
) -> None:
    cursor_x = x
    for label, color in items:
        svg.rect(cursor_x, y - 11, 18, 18, fill=color, stroke=AXIS_COLOR, stroke_width=1, rx=3)
        svg.text(cursor_x + 26, y + 3, label, size=text_size, anchor="start")
        cursor_x += gap


def draw_value_grid(
    svg: SvgDocument,
    *,
    x: float,
    y: float,
    width: float,
    height: float,
    y_max: float,
    tick_count: int = 5,
    label_size: int = 11,
) -> None:
    for tick in range(tick_count + 1):
        tick_value = y_max * tick / tick_count
        tick_y = y + height - ((tick_value / y_max) * height)
        svg.line(x, tick_y, x + width, tick_y, stroke=GRID_COLOR, stroke_width=1)
        svg.text(x - 10, tick_y + 4, compact_number(tick_value), size=label_size, anchor="end", fill="#4E5865")


def draw_category_axis(
    svg: SvgDocument,
    *,
    centers: list[float],
    labels: list[str],
    y: float,
    label_offset: float,
    label_size: int,
    rotate: float | None = None,
) -> None:
    for center, label in zip(centers, labels):
        svg.line(center, y, center, y + 6, stroke=AXIS_COLOR, stroke_width=1)
        svg.text(center, y + label_offset, label, size=label_size, rotate=rotate)


class SvgDocument:
    def __init__(self, width: int, height: int, background: str = BACKGROUND) -> None:
        self.width = width
        self.height = height
        self.elements: list[str] = []
        self.rect(0, 0, width, height, fill=background, stroke="none")

    def add(self, raw_svg: str) -> None:
        self.elements.append(raw_svg)

    def rect(
        self,
        x: float,
        y: float,
        width: float,
        height: float,
        *,
        fill: str = "none",
        stroke: str = AXIS_COLOR,
        stroke_width: float = 1.0,
        rx: float = 0.0,
        opacity: float | None = None,
    ) -> None:
        opacity_attr = f' opacity="{opacity:.3f}"' if opacity is not None else ""
        self.add(
            (
                f'<rect x="{x:.2f}" y="{y:.2f}" width="{width:.2f}" '
                f'height="{height:.2f}" fill="{fill}" stroke="{stroke}" '
                f'stroke-width="{stroke_width:.2f}" rx="{rx:.2f}"{opacity_attr} />'
            )
        )

    def line(
        self,
        x1: float,
        y1: float,
        x2: float,
        y2: float,
        *,
        stroke: str = AXIS_COLOR,
        stroke_width: float = 1.0,
        dasharray: str | None = None,
        opacity: float | None = None,
    ) -> None:
        extra = []
        if dasharray:
            extra.append(f'stroke-dasharray="{dasharray}"')
        if opacity is not None:
            extra.append(f'opacity="{opacity:.3f}"')
        suffix = " " + " ".join(extra) if extra else ""
        self.add(
            (
                f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" '
                f'stroke="{stroke}" stroke-width="{stroke_width:.2f}"{suffix} />'
            )
        )

    def polyline(
        self,
        points: list[tuple[float, float]],
        *,
        stroke: str,
        stroke_width: float = 2.0,
        fill: str = "none",
        opacity: float | None = None,
    ) -> None:
        point_text = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
        opacity_attr = f' opacity="{opacity:.3f}"' if opacity is not None else ""
        self.add(
            (
                f'<polyline points="{point_text}" fill="{fill}" stroke="{stroke}" '
                f'stroke-width="{stroke_width:.2f}" stroke-linecap="round" '
                f'stroke-linejoin="round"{opacity_attr} />'
            )
        )

    def circle(
        self,
        cx: float,
        cy: float,
        radius: float,
        *,
        fill: str,
        stroke: str = "#FFFFFF",
        stroke_width: float = 1.4,
    ) -> None:
        self.add(
            (
                f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{radius:.2f}" fill="{fill}" '
                f'stroke="{stroke}" stroke-width="{stroke_width:.2f}" />'
            )
        )

    def text(
        self,
        x: float,
        y: float,
        value: str,
        *,
        size: int = 14,
        fill: str = TEXT_COLOR,
        anchor: str = "middle",
        weight: str = "400",
        rotate: float | None = None,
        family: str = "Segoe UI, Arial, sans-serif",
    ) -> None:
        transform = f' transform="rotate({rotate:.2f} {x:.2f} {y:.2f})"' if rotate is not None else ""
        self.add(
            (
                f'<text x="{x:.2f}" y="{y:.2f}" fill="{fill}" font-size="{size}" '
                f'font-family="{family}" font-weight="{weight}" text-anchor="{anchor}"'
                f'{transform}>{escape(str(value))}</text>'
            )
        )

    def to_svg(self) -> str:
        payload = "\n".join(self.elements)
        return (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" '
            f'height="{self.height}" viewBox="0 0 {self.width} {self.height}">\n'
            f"{payload}\n"
            "</svg>\n"
        )


def find_headless_browser() -> str:
    candidates: list[str] = []

    if os.name == "nt":
        candidates.extend(
            [
                r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
                r"C:\Program Files\Google\Chrome\Application\chrome.exe",
                r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
            ]
        )

    for name in ("msedge.exe", "chrome.exe", "chromium.exe", "microsoft-edge", "google-chrome", "chromium", "chromium-browser"):
        resolved = shutil.which(name)
        if resolved:
            candidates.append(resolved)

    seen: set[str] = set()
    for candidate in candidates:
        if candidate and candidate not in seen and Path(candidate).exists():
            seen.add(candidate)
            return candidate

    raise RuntimeError("Nessun browser headless compatibile trovato. Installa o rendi disponibile Edge, Chrome o Chromium.")


def render_png(document: SvgDocument, output_path: Path) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    browser = find_headless_browser()

    with tempfile.TemporaryDirectory(prefix="parsir-grafici-") as temp_dir:
        temp_svg = Path(temp_dir) / "chart.svg"
        temp_svg.write_text(document.to_svg(), encoding="utf-8", newline="\n")
        command = [
            browser,
            "--headless",
            "--disable-gpu",
            "--hide-scrollbars",
            f"--window-size={document.width},{document.height}",
            f"--screenshot={output_path}",
            temp_svg.resolve().as_uri(),
        ]
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    return output_path
