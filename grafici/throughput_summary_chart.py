from __future__ import annotations

from pathlib import Path

from .chart_utils import (
    AXIS_COLOR,
    MODE_COLORS,
    MODE_LABELS,
    MODE_ORDER,
    MODEL_ORDER,
    MVM_MODE_ORDER,
    OUT_DIR,
    SvgDocument,
    band_centers,
    draw_category_axis,
    draw_model_header,
    draw_value_grid,
    ensure_output_dir,
    mix,
    nice_upper_bound,
    render_png,
)


SUMMARY_DATA = [
    {"model": "phold", "mode": "grid_ckpt", "variant": "full", "throughput": 44614.073698},
    {"model": "phold", "mode": "grid_ckpt_bs", "variant": "full", "throughput": 2948.066650},
    {"model": "phold", "mode": "chunk_ckpt", "variant": "full", "throughput": 28623.119922},
    {"model": "phold", "mode": "chunk_full_ckpt", "variant": "full", "throughput": 3361.179997},
    {"model": "phold", "mode": "mmap_mv", "variant": "full", "throughput": 20457.680143},
    {"model": "phold", "mode": "mmap_mv", "variant": "best", "throughput": 45623.839844, "page_size": 128},
    {"model": "phold", "mode": "mmap_mv_store", "variant": "full", "throughput": 20423.573242},
    {"model": "phold", "mode": "mmap_mv_store", "variant": "best", "throughput": 58053.647005, "page_size": 128},
    {"model": "phold", "mode": "mmap_mv_store_grid", "variant": "full", "throughput": 56321.493750},
    {"model": "phold", "mode": "mmap_mv_store_grid", "variant": "best", "throughput": 62836.466406, "page_size": 512},

    {"model": "pcs", "mode": "grid_ckpt", "variant": "full", "throughput": 276908.645313},
    {"model": "pcs", "mode": "grid_ckpt_bs", "variant": "full", "throughput": 46506.459766},
    {"model": "pcs", "mode": "chunk_ckpt", "variant": "full", "throughput": 58834.399740},
    {"model": "pcs", "mode": "chunk_full_ckpt", "variant": "full", "throughput": 248375.586458},
    {"model": "pcs", "mode": "mmap_mv", "variant": "full", "throughput": 93036.346354},
    {"model": "pcs", "mode": "mmap_mv", "variant": "best", "throughput": 137634.780729, "page_size": 256},
    {"model": "pcs", "mode": "mmap_mv_store", "variant": "full", "throughput": 235957.155729},
    {"model": "pcs", "mode": "mmap_mv_store", "variant": "best", "throughput": 263027.998958, "page_size": 1024},
    {"model": "pcs", "mode": "mmap_mv_store_grid", "variant": "full", "throughput": 175180.967708},
    {"model": "pcs", "mode": "mmap_mv_store_grid", "variant": "best", "throughput": 203793.599479, "page_size": 512},

    {"model": "highway", "mode": "grid_ckpt", "variant": "full", "throughput": 512164.340625},
    {"model": "highway", "mode": "grid_ckpt_bs", "variant": "full", "throughput": 28291.486198},
    {"model": "highway", "mode": "chunk_ckpt", "variant": "full", "throughput": 566732.789583},
    {"model": "highway", "mode": "chunk_full_ckpt", "variant": "full", "throughput": 419603.495833},
    {"model": "highway", "mode": "mmap_mv", "variant": "full", "throughput": 286315.508333},
    {"model": "highway", "mode": "mmap_mv", "variant": "best", "throughput": 392242.700000, "page_size": 512},
    {"model": "highway", "mode": "mmap_mv_store", "variant": "full", "throughput": 253575.646354},
    {"model": "highway", "mode": "mmap_mv_store", "variant": "best", "throughput": 385521.377083, "page_size": 1024},
    {"model": "highway", "mode": "mmap_mv_store_grid", "variant": "full", "throughput": 281229.052083},
    {"model": "highway", "mode": "mmap_mv_store_grid", "variant": "best", "throughput": 365145.765625, "page_size": 512},
]

PANEL = {"top": 215, "height": 460, "width": 470, "gap": 52, "left": 110}


def build_data() -> dict[str, dict[str, dict[str, float | int]]]:
    data = {model: {mode: {} for mode in MODE_ORDER} for model in MODEL_ORDER}
    for item in SUMMARY_DATA:
        slot = data[item["model"]][item["mode"]]
        slot[item["variant"]] = float(item["throughput"])
        if "page_size" in item:
            slot["page_size"] = int(item["page_size"])
    return data


def draw_style_legend(svg: SvgDocument, *, x: float, y: float) -> None:
    svg.rect(x, y - 10, 18, 18, fill="#B7C1CE", stroke=AXIS_COLOR, stroke_width=1, rx=3)
    svg.text(x + 26, y + 3, "standard config", size=13, anchor="start")
    svg.rect(x + 160, y - 10, 18, 18, fill="#5A6472", stroke=AXIS_COLOR, stroke_width=1, rx=3)
    svg.text(x + 186, y + 3, "best page size", size=13, anchor="start")


def generate_summary_chart() -> list[Path]:
    ensure_output_dir()
    data = build_data()
    y_max_by_model = {
        model: nice_upper_bound(
            max(float(value) for mode_data in data[model].values() for key, value in mode_data.items() if key in {"full", "best"}) * 1.08
        )
        for model in MODEL_ORDER
    }

    svg = SvgDocument(1780, 860)
    svg.text(890, 48, "Throughput summary - full matrix vs best page size", size=28, weight="700")
    svg.text(
        890,
        76,
        "Per le varianti MVM, la barra chiara mostra il throughput della configurazione standard (pagina 4096) e la barra scura il miglior throughput osservato nel page-size sweep. Le current config dei workload sono sotto il titolo del pannello.",
        size=15,
        fill="#55606E",
    )
    draw_style_legend(svg, x=500, y=100)

    for panel_index, model in enumerate(MODEL_ORDER):
        panel_x = PANEL["left"] + panel_index * (PANEL["width"] + PANEL["gap"])
        panel_y = PANEL["top"]
        panel_width = PANEL["width"]
        panel_height = PANEL["height"]
        panel_y_max = y_max_by_model[model]

        draw_model_header(svg, model=model, x=panel_x + (panel_width / 2), title_y=panel_y - 48)
        svg.rect(panel_x, panel_y, panel_width, panel_height, fill="none", stroke=AXIS_COLOR, stroke_width=1.2)
        draw_value_grid(svg, x=panel_x, y=panel_y, width=panel_width, height=panel_height, y_max=panel_y_max, label_size=13)

        centers, band = band_centers(panel_x, panel_width, len(MODE_ORDER))
        single_bar_width = band * 0.54
        paired_bar_width = band * 0.24
        paired_offset = band * 0.16

        for mode, center in zip(MODE_ORDER, centers):
            svg.line(center, panel_y, center, panel_y + panel_height, stroke="#EEF2F6", stroke_width=1)
            mode_data = data[model][mode]
            base_color = MODE_COLORS[mode]
            full_color = mix(base_color, "#FFFFFF", 0.12)
            best_color = mix(base_color, "#111827", 0.22)

            if mode in MVM_MODE_ORDER:
                full_value = float(mode_data["full"])
                best_value = float(mode_data["best"])
                full_height = (full_value / panel_y_max) * panel_height
                best_height = (best_value / panel_y_max) * panel_height
                full_center = center - paired_offset
                best_center = center + paired_offset
                svg.rect(full_center - (paired_bar_width / 2), panel_y + panel_height - full_height, paired_bar_width, full_height, fill=full_color, stroke=mix(full_color, "#1A1A1A", 0.22), stroke_width=1.1, rx=4)
                svg.rect(best_center - (paired_bar_width / 2), panel_y + panel_height - best_height, paired_bar_width, best_height, fill=best_color, stroke=mix(best_color, "#1A1A1A", 0.22), stroke_width=1.1, rx=4)
                label_y = max(panel_y + 14, panel_y + panel_height - best_height - 8)
                svg.text(best_center, label_y, f"ps{int(mode_data['page_size'])}", size=10, fill="#3F4A57")
            else:
                full_value = float(mode_data["full"])
                full_height = (full_value / panel_y_max) * panel_height
                svg.rect(center - (single_bar_width / 2), panel_y + panel_height - full_height, single_bar_width, full_height, fill=full_color, stroke=mix(full_color, "#1A1A1A", 0.22), stroke_width=1.1, rx=4)

        draw_category_axis(
            svg,
            centers=centers,
            labels=[MODE_LABELS[mode] for mode in MODE_ORDER],
            y=panel_y + panel_height,
            label_offset=26,
            label_size=11,
            rotate=-35,
        )
        svg.text(panel_x + (panel_width / 2), panel_y + panel_height + 86, "Backend / variante", size=14, weight="600")

    svg.text(36, PANEL["top"] + (PANEL["height"] / 2), "Throughput medio (eventi/s)", size=15, rotate=-90, weight="600")
    output_path = OUT_DIR / "throughput_summary.png"
    return [render_png(svg, output_path)]
