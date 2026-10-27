"""Build the static composer Pages artifact from an explicit asset allowlist."""
from pathlib import Path
import argparse
import shutil

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ("index.html", "app.css", "v4-app.js", "v4-grammar.js", "builtins-v4.json")

def build(destination):
    destination = destination.resolve()
    if destination != ROOT / "build" / "pages":
        raise ValueError("site output must be the repository build/pages directory")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    source = ROOT / "docs/exploration/authoring"
    for name in ASSETS:
        shutil.copy2(source / name, destination / name)
    shutil.copy2(ROOT / "LICENSE", destination / "LICENSE")
    (destination / ".nojekyll").write_text("", encoding="utf-8")
    return destination

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    print(build(ROOT / "build/pages"))
