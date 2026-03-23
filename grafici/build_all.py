from __future__ import annotations

if __package__ in (None, ""):
    import sys
    from pathlib import Path

    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from grafici.full_matrix_charts import generate_full_matrix_charts
from grafici.page_size_charts import generate_page_size_charts
from grafici.throughput_summary_chart import generate_summary_chart


def main() -> None:
    for path in [*generate_full_matrix_charts(), *generate_page_size_charts(), *generate_summary_chart()]:
        print(path)


if __name__ == "__main__":
    main()
