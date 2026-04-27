from __future__ import annotations

import math
from pathlib import Path

from .chart_utils import (
    AXIS_COLOR,
    FULL_MATRIX_PATH,
    GRID_COLOR,
    MODEL_LABELS,
    MODEL_ORDER,
    MODE_COLORS,
    MODE_LABELS,
    MVM_MODE_ORDER,
    OUT_DIR,
    PAGE_SIZES_PATH,
    SvgDocument,
    compact_number,
    draw_category_axis,
    draw_model_header,
    draw_value_grid,
    ensure_output_dir,
    nice_upper_bound,
    read_tsv,
    render_png,
    to_float,
    to_int,
)


PAGE_ORDER = [128, 256, 512, 1024, 2048, 4096]
PAGE_LABELS = [str(value) for value in PAGE_ORDER]
PAGE_LOG_MIN = math.log2(PAGE_ORDER[0])
PAGE_LOG_MAX = math.log2(PAGE_ORDER[-1])
THROUGHPUT_PANEL = {"top": 215, "height": 420, "width": 470, "gap": 52, "left": 110}
METRIC_PANEL = {"top": 255, "height": 205, "width": 460, "gap": 52, "left": 110, "row_gap": 94}


def load_page_size_rows() -> list[dict[str, float | int | str]]:
    rows = [
        row
        for row in read_tsv(PAGE_SIZES_PATH)
        if row["status"] == "OK" and row["throughput_mean"]
    ]
    defaults = [
        row
        for row in read_tsv(FULL_MATRIX_PATH)
        if row["status"] == "OK" and row["throughput_mean"] and row["mode"] in MVM_MODE_ORDER
    ]

    parsed = [
        {
            "model": row["model"],
            "mode": row["mode"],
            "page_size": to_int(row["page_size"]),
            "throughput_mean": to_float(row["throughput_mean"]),
            "throughput_ci": to_float(row["throughput_ci"]),
            "rollbacks": to_int(row["rollbacks"]),
            "epochs": to_int(row["epochs"]),
            "filtered_events": to_int(row["filtered_events"]),
        }
        for row in rows
    ]
    parsed.extend(
        {
            "model": row["model"],
            "mode": row["mode"],
            "page_size": 4096,
            "throughput_mean": to_float(row["throughput_mean"]),
            "throughput_ci": to_float(row["throughput_ci"]),
            "rollbacks": to_int(row["rollbacks"]),
            "epochs": to_int(row["epochs"]),
            "filtered_events": to_int(row["filtered_events"]),
        }
        for row in defaults
    )
    parsed.sort(key=lambda item: (str(item["model"]), str(item["mode"]), int(item["page_size"])))
    return parsed


def page_to_x(page_size: int, x: float, width: float) -> float:
    ratio = (math.log2(page_size) - PAGE_LOG_MIN) / (PAGE_LOG_MAX - PAGE_LOG_MIN)
    return x + ratio * width


def draw_page_axis(svg: SvgDocument, *, x: float, y: float, width: float) -> None:
    centers = [page_to_x(page_size, x, width) for page_size in PAGE_ORDER]
    draw_category_axis(svg, centers=centers, labels=PAGE_LABELS, y=y, label_offset=22, label_size=11)


def draw_line_legend(svg: SvgDocument, *, x: float, y: float) -> None:
    cursor_x = x
    for mode in MVM_MODE_ORDER:
        color = MODE_COLORS[mode]
        svg.line(cursor_x, y, cursor_x + 28, y, stroke=color, stroke_width=3)
        svg.circle(cursor_x + 14, y, 4.5, fill=color)
        svg.text(cursor_x + 38, y + 4, MODE_LABELS[mode], size=13, anchor="start")
        cursor_x += 172


def build_throughput_chart(rows: list[dict[str, float | int | str]], output_path: Path) -> Path:
    y_max_by_model = {
        model: nice_upper_bound(
            max(float(row["throughput_mean"]) + float(row["throughput_ci"]) for row in rows if row["model"] == model) * 1.08
        )
        for model in MODEL_ORDER
    }

    svg = SvgDocument(1780, 760)
    svg.text(890, 48, "MVM page-size sweep - throughput", size=28, weight="700")
    svg.text(
        890,
        76,
        "Throughput sensitivity of the MVM variants to page size. The point at 4096 bytes marks the default used in the full-matrix comparison; the current workload configuration is shown below each panel title.",
        size=15,
        fill="#55606E",
    )
    draw_line_legend(svg, x=430, y=100)

    for panel_index, model in enumerate(MODEL_ORDER):
        panel_x = THROUGHPUT_PANEL["left"] + panel_index * (THROUGHPUT_PANEL["width"] + THROUGHPUT_PANEL["gap"])
        panel_y = THROUGHPUT_PANEL["top"]
        panel_height = THROUGHPUT_PANEL["height"]
        panel_width = THROUGHPUT_PANEL["width"]
        panel_y_max = y_max_by_model[model]

        draw_model_header(svg, model=model, x=panel_x + (panel_width / 2), title_y=panel_y - 48)
        default_x = page_to_x(4096, panel_x, panel_width)
        svg.rect(default_x - 16, panel_y, 32, panel_height, fill="#FFF4D6", stroke="none", opacity=0.75)
        svg.text(default_x, panel_y - 6, "default", size=11, fill="#8A5A00")
        svg.rect(panel_x, panel_y, panel_width, panel_height, fill="none", stroke=AXIS_COLOR, stroke_width=1.2)
        draw_value_grid(svg, x=panel_x, y=panel_y, width=panel_width, height=panel_height, y_max=panel_y_max, label_size=13)

        page_centers = [page_to_x(page_size, panel_x, panel_width) for page_size in PAGE_ORDER]
        for tick_x in page_centers:
            svg.line(tick_x, panel_y, tick_x, panel_y + panel_height, stroke="#EEF2F6", stroke_width=1)

        for mode in MVM_MODE_ORDER:
            color = MODE_COLORS[mode]
            series = [row for row in rows if row["model"] == model and row["mode"] == mode]
            points = []
            for row in series:
                px = page_to_x(int(row["page_size"]), panel_x, panel_width)
                py = panel_y + panel_height - ((float(row["throughput_mean"]) / panel_y_max) * panel_height)
                points.append((px, py))
                ci = float(row["throughput_ci"])
                if ci > 0:
                    top_y = panel_y + panel_height - (((float(row["throughput_mean"]) + ci) / panel_y_max) * panel_height)
                    bottom_y = panel_y + panel_height - (((max(float(row["throughput_mean"]) - ci, 0)) / panel_y_max) * panel_height)
                    svg.line(px, top_y, px, bottom_y, stroke=color, stroke_width=1.4)
                    svg.line(px - 5, top_y, px + 5, top_y, stroke=color, stroke_width=1.4)
                    svg.line(px - 5, bottom_y, px + 5, bottom_y, stroke=color, stroke_width=1.4)
            svg.polyline(points, stroke=color, stroke_width=3)
            for px, py in points:
                svg.circle(px, py, 5.5, fill=color)

        draw_page_axis(svg, x=panel_x, y=panel_y + panel_height, width=panel_width)
        svg.text(panel_x + (panel_width / 2), panel_y + panel_height + 62, "Page size (bytes)", size=14, weight="600")

    svg.text(36, THROUGHPUT_PANEL["top"] + (THROUGHPUT_PANEL["height"] / 2), "Average throughput (events/s)", size=15, rotate=-90, weight="600")
    return render_png(svg, output_path)


def draw_metric_panel(
    svg: SvgDocument,
    *,
    x: float,
    y: float,
    width: float,
    height: float,
    title: str,
    model: str,
    metric: str,
    rows: list[dict[str, float | int | str]],
) -> None:
    series = [row for row in rows if row["model"] == model]
    panel_max = nice_upper_bound(max(float(row[metric]) for row in series) * 1.08 or 1.0)

    svg.text(x + (width / 2), y - 16, title, size=17, weight="700")
    svg.rect(x, y, width, height, fill="none", stroke=AXIS_COLOR, stroke_width=1.1)
    draw_value_grid(svg, x=x, y=y, width=width, height=height, y_max=panel_max)

    page_centers = [page_to_x(page_size, x, width) for page_size in PAGE_ORDER]
    for tick_x in page_centers:
        svg.line(tick_x, y, tick_x, y + height, stroke="#EEF2F6", stroke_width=1)

    for mode in MVM_MODE_ORDER:
        color = MODE_COLORS[mode]
        points = [
            (
                page_to_x(int(row["page_size"]), x, width),
                y + height - ((float(row[metric]) / panel_max) * height),
            )
            for row in rows
            if row["model"] == model and row["mode"] == mode
        ]
        svg.polyline(points, stroke=color, stroke_width=2.6)
        for px, py in points:
            svg.circle(px, py, 4.2, fill=color)

    draw_page_axis(svg, x=x, y=y + height, width=width)


def build_metrics_chart(rows: list[dict[str, float | int | str]], output_path: Path) -> Path:
    svg = SvgDocument(1780, 1240)
    svg.text(890, 46, "MVM page-size sweep - context metrics", size=28, weight="700")
    svg.text(
        890,
        74,
        "Variation of rollbacks, epochs, and filtered events across page sizes for the three MVM variants, with the current configuration shown above each column.",
        size=15,
        fill="#55606E",
    )
    draw_line_legend(svg, x=430, y=100)

    metrics = [("rollbacks", "Rollback"), ("epochs", "Epoch"), ("filtered_events", "Filtered events")]

    for column_index, model in enumerate(MODEL_ORDER):
        panel_x = METRIC_PANEL["left"] + column_index * (METRIC_PANEL["width"] + METRIC_PANEL["gap"])
        draw_model_header(svg, model=model, x=panel_x + (METRIC_PANEL["width"] / 2), title_y=METRIC_PANEL["top"] - 98)

    for row_index, (metric_key, metric_label) in enumerate(metrics):
        for column_index, model in enumerate(MODEL_ORDER):
            panel_x = METRIC_PANEL["left"] + column_index * (METRIC_PANEL["width"] + METRIC_PANEL["gap"])
            panel_y = METRIC_PANEL["top"] + row_index * (METRIC_PANEL["height"] + METRIC_PANEL["row_gap"])
            draw_metric_panel(
                svg,
                x=panel_x,
                y=panel_y,
                width=METRIC_PANEL["width"],
                height=METRIC_PANEL["height"],
                title=metric_label,
                model=model,
                metric=metric_key,
                rows=rows,
            )

    svg.text(30, 540, "Absolute value", size=15, rotate=-90, weight="600")
    svg.text(890, 1188, "Page size (bytes)", size=15, weight="600")
    return render_png(svg, output_path)


def generate_page_size_charts() -> list[Path]:
    ensure_output_dir()
    rows = load_page_size_rows()
    return [
        build_throughput_chart(rows, OUT_DIR / "page_size_throughput.png"),
        build_metrics_chart(rows, OUT_DIR / "page_size_metrics.png"),
    ]
