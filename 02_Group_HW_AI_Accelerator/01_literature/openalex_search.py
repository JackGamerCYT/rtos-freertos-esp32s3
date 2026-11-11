"""
Tra cứu OpenAlex và xuất bảng paper (CSV + Markdown). Chạy trên Colab hoặc PC có mạng:
    python openalex_search.py --per 25 --since 2016
OpenAlex miễn phí, không cần key (nên thêm email vào mailto để vào "polite pool").
"""
import argparse, csv, json, urllib.parse, urllib.request

QUERIES = [
    "power-of-two quantization FPGA accelerator",
    "shift-add neural network accelerator",
    "logarithmic quantization hardware accelerator",
    "mixed-precision systolic array FPGA",
    "runtime reconfigurable precision neural network accelerator",
    "multiplier-less CNN accelerator FPGA",
]


def search(q, per, since, mailto):
    params = {
        "search": q,
        "filter": f"from_publication_date:{since}-01-01",
        "sort": "cited_by_count:desc",
        "per-page": per,
        "select": "id,doi,title,publication_year,cited_by_count,primary_location,open_access",
    }
    if mailto:
        params["mailto"] = mailto
    url = "https://api.openalex.org/works?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=30) as r:
        return json.load(r)["results"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per", type=int, default=20)
    ap.add_argument("--since", type=int, default=2016)
    ap.add_argument("--mailto", default="")
    a = ap.parse_args()
    rows, seen = [], set()
    for q in QUERIES:
        for w in search(q, a.per, a.since, a.mailto):
            if w["id"] in seen:
                continue
            seen.add(w["id"])
            src = ((w.get("primary_location") or {}).get("source") or {}).get("display_name", "")
            oa = (w.get("open_access") or {}).get("oa_url") or ""
            rows.append([q, w["title"], w["publication_year"], w["cited_by_count"], src, w.get("doi") or "", oa])
    rows.sort(key=lambda r: -r[3])
    with open("openalex_papers.csv", "w", newline="", encoding="utf-8") as f:
        csv.writer(f).writerows([["query", "title", "year", "cited", "venue", "doi", "oa_pdf"]] + rows)
    with open("openalex_papers.md", "w", encoding="utf-8") as f:
        f.write("| title | year | cited | venue | doi | pdf |\n|---|---|---|---|---|---|\n")
        for r in rows:
            f.write(f"| {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]} | {r[6]} |\n")
    print(f"{len(rows)} paper -> openalex_papers.csv / .md")


if __name__ == "__main__":
    main()
