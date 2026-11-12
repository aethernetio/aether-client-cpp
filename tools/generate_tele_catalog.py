# Copyright 2026 Aethernet Inc.
# Licensed under the Apache License, Version 2.0.
"""Generate deterministic, collision-free raw telemetry callsite IDs."""
import argparse
import hashlib
import json
from pathlib import Path
import re

p = argparse.ArgumentParser()
p.add_argument('--root', type=Path, required=True)
p.add_argument('--header', type=Path, required=True)
p.add_argument('--map', type=Path, required=True)
a = p.parse_args()
sites=[]
for folder in ('aether', 'examples', 'tests', 'tools'):
    for source in sorted((a.root/folder).rglob('*')):
        if source.suffix not in ('.h','.cpp','.cc','.hpp'): continue
        original=source.read_text(encoding='utf-8-sig')
        masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27',
                      lambda m: ''.join('\n' if c=='\n' else ' ' for c in m.group()), original, flags=re.S)
        lines=masked.splitlines(keepends=True)
        directive=False
        for i,line in enumerate(lines):
            if re.match(r'\s*#', line) or directive:
                directive=line.rstrip().endswith('\\')
                lines[i]='\n' if line.endswith('\n') else ''
        masked=''.join(lines)
        for match in re.finditer(r'\b(?:AE|AETHER)_TELED?_(?:DEBUG|INFO|WARNING|ERROR)(?:_TO)?\s*\(', masked):
            start=match.start(); end=match.end(); depth=1
            while end<len(masked) and depth:
                depth += (masked[end]=='(') - (masked[end]==')'); end+=1
            first=masked.count('\n',0,start)+1
            last=masked.count('\n',0,end)+1
            sites.append(dict(file=source.relative_to(a.root).as_posix(), line=first, last_line=last,
                              text=original.splitlines()[first-1].strip()))
sites.sort(key=lambda x:(x['file'],x['line']))
for previous,current in zip(sites,sites[1:]):
    if previous['file']==current['file'] and previous['last_line']>=current['line']:
        raise SystemExit(f"Telemetry callsites must occupy separate lines: {current['file']}:{current['line']}")
if len(sites)>30000: raise SystemExit('Too many Aether telemetry callsites')
catalog_id=hashlib.sha256(json.dumps(sites,sort_keys=True).encode()).hexdigest()
entries=[]
for index,site in enumerate(sites,1):
    site['index']=index
    entries.append('  Site{'+json.dumps(site['file'])+','+str(site['line'])+','+str(site['last_line'])+','+str(index)+'},')
a.header.parent.mkdir(parents=True,exist_ok=True)
a.header.write_text('#pragma once\n#include <array>\n#include <cstdint>\n#include <string_view>\nnamespace ae::aether_tele_internal {\nstruct Site { std::string_view file; std::uint32_t first,last,index; };\ninline constexpr std::array<Site,'+str(len(sites))+'> kSites{{\n'+'\n'.join(entries)+'\n}};\ninline constexpr std::string_view kCatalogId = \"'+catalog_id+'\";\n}\n')
a.map.write_text(json.dumps({'space':'aether','catalog_id':catalog_id,'payload_encoding':'utf8','sites':sites},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
