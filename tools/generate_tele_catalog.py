# Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
"""Generate SDK production IDs and exact offline map; excludes examples/tests."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--header", type=Path, required=True)
parser.add_argument("--map", type=Path, required=True)
parser.add_argument("--tele-tools", type=Path)
parser.add_argument("--profile", type=Path)
parser.add_argument("--numeric-root", type=Path)
args = parser.parse_args()
tools = args.tele_tools or Path(__file__).resolve().parents[2] / "aether-tele" / "tools"
sys.path.insert(0, str(tools.resolve()))
from build_catalog import build, write_if_changed
from telemetry_artifact import canonical

root = args.root.resolve()
output = args.map.resolve().parent / "aether-catalog"
with tempfile.TemporaryDirectory(prefix="aether-catalog-descriptor-") as temporary:
    descriptor = Path(temporary) / "catalog.json"
    repositories = {"aether": {"root": str(root)}, "tele": {"root": str(tools.resolve().parent)}}
    if args.numeric_root:
        repositories["numeric"] = {"root": str(args.numeric_root.resolve())}
    descriptor.write_text(json.dumps(dict(product="sdk", repositories=repositories,
        namespaces=[dict(name="aether", repository="aether", include=["aether/"],
                         macros=["AETHER_TELE", "AETHER_TELED"])], configuration={})))
    manifest = build(descriptor, output, args.profile)
space = manifest["namespaces"][0]
map_bytes = (output / "catalogs" / (space["catalog_sha256"] + ".json")).read_bytes()
mapping = json.loads(map_bytes)
sites = sorted(mapping["sites"], key=lambda site: (site["file"], site["line"]))
entries = ["  Site{" + json.dumps(site["file"]) + "," + str(site["line"]) + "," +
           str(site["last_line"]) + "," + str(site["index"]) + "}," for site in sites]
revision = manifest["build"]["repositories"]["aether"]
header = "#pragma once\n#include <array>\n#include <cstdint>\n#include <string_view>\nnamespace ae::aether_tele_internal {\n"
header += "struct Site { std::string_view file; std::uint32_t first,last,index; };\n"
header += "inline constexpr std::array<Site," + str(len(entries)) + "> kSites{{\n" + "\n".join(entries) + "\n}};\n"
for key, value in [("kCatalogId",space["catalog_sha256"]),("kSchemaId",space["schema_sha256"]),("kCommit",revision["commit"]),
                   ("kBuildId",manifest["build"]["build_id"]),
                   ("kBuildJson",canonical(manifest["build"]).decode()),
                   ("kManifestJson",canonical(manifest).decode())]:
    header += "inline constexpr std::string_view " + key + " = " + json.dumps(value) + ";\n"
header += "inline constexpr bool kDirty = " + str(revision["dirty"]).lower() + ";\n}\n"
args.header.parent.mkdir(parents=True, exist_ok=True)
write_if_changed(args.header, header.encode("utf-8"))
args.map.parent.mkdir(parents=True, exist_ok=True)
write_if_changed(args.map, map_bytes)
