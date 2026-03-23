from __future__ import annotations

from pathlib import Path

from .chart_utils import (
    AXIS_COLOR,
    FULL_MATRIX_PATH,
    MODE_COLORS,
    MODE_LABELS,
    MODE_ORDER,
    MODEL_LABELS,
    MODEL_ORDER,
    OUT_DIR,
    SvgDocument,
    band_centers,
    draw_category_axis,
    draw_legend,
    draw_value_grid,
    ensure_output_dir,
    mix,
    nice_upper_bound,
    read_tsv,
    render_png,
    to_float,
    to_int,
)


THROUGHPUT_PANEL = {"top": 170, "height": 420, "width": 470, "gap": 52, "left": 110}
METRIC_PANEL = {"top": 185, "height": 200, "width": 460, "gap": 52, "left": 110, "row_gap": 135}


def load_full_matrix_rows() -> list[dict[str, str]]:
    return [
        row
        for row in read_tsv(FULL_MATRIX_PATH)
        if row["status"] == "OK" and row["throughput_mean"] and row["mode"] != "plain"
    ]


def build_throughput_chart(rows: list[dict[str, str]], output_path: Path) -> Path:
    data = {
        model: {
            row["mode"]: (to_float(row["throughput_mean"]), to_float(row["throughput_ci"]))
            for row in rows
            if row["model"] == model
        }
        for model in MODEL_ORDER
    }
    y_max_by_model = {
        model: nice_upper_bound(max(mean + ci for mean, ci in data[model].values()) * 1.08)
        for model in MODEL_ORDER
    }

    svg = SvgDocument(1780, 760)
    svg.text(890, 48, "Full Matrix - throughput overview", size=28, weight="700")
    svg.text(
        890,
        76,
        "Confronto tra i sette backend su PHOLD, PCS e HIGHWAY con intervalli di confidenza. Ogni pannello usa la propria scala verticale.",
        size=15,
        fill="#55606E",
    )
    draw_legend(svg, items=[(MODE_LABELS[mode], MODE_COLORS[mode]) for mode in MODE_ORDER], x=118, y=116)

    for panel_index, model in enumerate(MODEL_ORDER):
        panel_x = THROUGHPUT_PANEL["left"] + panel_index * (THROUGHPUT_PANEL["width"] + THROUGHPUT_PANEL["gap"])
        panel_y = THROUGHPUT_PANEL["top"]
        panel_height = THROUGHPUT_PANEL["height"]
        panel_width = THROUGHPUT_PANEL["width"]
        panel_y_max = y_max_by_model[model]

        svg.text(panel_x + (panel_width / 2), panel_y - 24, MODEL_LABELS[model], size=22, weight="700")
        svg.rect(panel_x, panel_y, panel_width, panel_height, fill="none", stroke=AXIS_COLOR, stroke_width=1.2)
        draw_value_grid(svg, x=panel_x, y=panel_y, width=panel_width, height=panel_height, y_max=panel_y_max, label_size=13)

        centers, band = band_centers(panel_x, panel_width, len(MODE_ORDER))
        bar_width = band * 0.56

        for mode, center in zip(MODE_ORDER, centers):
            mean, ci = data[model][mode]
            bar_height = (mean / panel_y_max) * panel_height
            bar_y = panel_y + panel_height - bar_height
            bar_x = center - (bar_width / 2)
            color = MODE_COLORS[mode]
            svg.rect(bar_x, bar_y, bar_width, bar_height, fill=color, stroke=mix(color, "#1A1A1A", 0.22), stroke_width=1.2, rx=4)

            if ci > 0:
                top_value = min(mean + ci, panel_y_max)
                bottom_value = max(mean - ci, 0)
                top_y = panel_y + panel_height - ((top_value / panel_y_max) * panel_height)
                bottom_y = panel_y + panel_height - ((bottom_value / panel_y_max) * panel_height)
                svg.line(center, top_y, center, bottom_y, stroke="#1E2935", stroke_width=1.5)
                svg.line(center - 7, top_y, center + 7, top_y, stroke="#1E2935", stroke_width=1.5)
                svg.line(center - 7, bottom_y, center + 7, bottom_y, stroke="#1E2935", stroke_width=1.5)

        draw_category_axis(
            svg,
            centers=centers,
            labels=[MODE_LABELS[mode] for mode in MODE_ORDER],
            y=panel_y + panel_height,
            label_offset=32,
            label_size=12,
            rotate=-35,
        )
        svg.text(panel_x + (panel_width / 2), panel_y + panel_height + 92, "Backend", size=14, weight="600")

    svg.text(36, THROUGHPUT_PANEL["top"] + (THROUGHPUT_PANEL["height"] / 2), "Throughput medio (eventi/s)", size=15, rotate=-90, weight="600")
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
    rows: list[dict[str, str]],
) -> None:
    values = {
        row["mode"]: to_int(row[metric])
        for row in rows
        if row["model"] == model
    }
    panel_max = nice_upper_bound(max(values.values()) * 1.08 or 1.0)

    svg.text(x + (width / 2), y - 16, title, size=17, weight="700")
    svg.rect(x, y, width, height, fill="none", stroke=AXIS_COLOR, stroke_width=1.1)
    draw_value_grid(svg, x=x, y=y, width=width, height=height, y_max=panel_max)

    centers, band = band_centers(x, width, len(MODE_ORDER))
    bar_width = band * 0.56

    for mode, center in zip(MODE_ORDER, centers):
        value = values[mode]
        svg.line(center, y, center, y + height, stroke="#EEF2F6", stroke_width=1)
        bar_height = (value / panel_max) * height if panel_max else 0
        bar_y = y + height - bar_height
        bar_x = center - (bar_width / 2)
        color = MODE_COLORS[mode]
        svg.rect(bar_x, bar_y, bar_width, bar_height, fill=color, stroke=mix(color, "#1A1A1A", 0.22), stroke_width=1.1, rx=4)

    draw_category_axis(
        svg,
        centers=centers,
        labels=[MODE_LABELS[mode] for mode in MODE_ORDER],
        y=y + height,
        label_offset=22,
        label_size=10,
        rotate=-35,
    )


def build_metrics_chart(rows: list[dict[str, str]], output_path: Path) -> Path:
    svg = SvgDocument(1780, 1280)
    svg.text(890, 46, "Full Matrix - metriche di contesto", size=28, weight="700")
    svg.text(
        890,
        74,
        "Confronto tra backend tramite istogrammi di rollback, epoch e filtered events. Un pannello per workload e metrica.",
        size=15,
        fill="#55606E",
    )
    draw_legend(svg, items=[(MODE_LABELS[mode], MODE_COLORS[mode]) for mode in MODE_ORDER], x=118, y=114)

    metrics = [("rollbacks", "Rollback"), ("epochs", "Epoch"), ("filtered_events", "Filtered events")]

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
                title=f"{MODEL_LABELS[model]} - {metric_label}",
                model=model,
                metric=metric_key,
                rows=rows,
            )

    svg.text(30, 610, "Valore assoluto", size=15, rotate=-90, weight="600")
    svg.text(890, 1228, "Backend", size=15, weight="600")
    return render_png(svg, output_path)


def generate_full_matrix_charts() -> list[Path]:
    ensure_output_dir()
    rows = load_full_matrix_rows()
    return [
        build_throughput_chart(rows, OUT_DIR / "full_matrix_throughput.png"),
        build_metrics_chart(rows, OUT_DIR / "full_matrix_metrics.png"),
    ]
