import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CSV_FILE  = REPO_ROOT / "data" / "benchmark_results.csv"
PDF_FILE  = REPO_ROOT / "data" / "benchmark_report.pdf"

def generate_report():
    if not CSV_FILE.exists():
        print(f"Error: {CSV_FILE} not found. Run bench first and redirect output to CSV.")
        return

    df = pd.read_csv(CSV_FILE)

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))

    ax = axes[0]
    ax.plot(df['n'], df['kd_us'],    marker='o', label='KD-Tree (fresh)')
    ax.plot(df['n'], df['brute_us'], marker='s', label='Brute Force')
    ax.set_xscale('log'); ax.set_yscale('log')
    ax.set_xlabel('Number of Points (N)')
    ax.set_ylabel('Average Query Time (us)')
    ax.set_title('Query Time: KD-Tree vs Brute Force')
    ax.legend(); ax.grid(True, which="both", ls="--", alpha=0.5)

    ax = axes[1]
    ax.plot(df['n'], df['kd_us'],          marker='o', label='KD-Tree (fresh)')
    ax.plot(df['n'], df['kd_degraded_us'], marker='x', label='KD-Tree (40% degraded)')
    ax.set_xscale('log')
    ax.set_xlabel('Number of Points (N)')
    ax.set_ylabel('Average Query Time (us)')
    ax.set_title('Query Time: Fresh vs Degraded')
    ax.legend(); ax.grid(True, alpha=0.5)

    ax = axes[2]
    k_vals        = [10, 50, 100, 500, 1000]
    full_rebuild  = [5000 for _ in k_vals]
    incr_update   = [k * 1.5 for k in k_vals]
    ax.plot(k_vals, full_rebuild, color='red',  label='Full Rebuild O(N log N)')
    ax.plot(k_vals, incr_update,  color='blue', marker='o', label='Incremental O(k log N)')
    ax.set_xlabel('Affected Neighbours (k)')
    ax.set_ylabel('Update Time (us)')
    ax.set_title('Incremental Update vs Full Rebuild (Theoretical)')
    ax.legend(); ax.grid(True, alpha=0.5)

    plt.tight_layout()
    plt.savefig(PDF_FILE)
    print(f"Report saved to {PDF_FILE}")

if __name__ == '__main__':
    generate_report()
