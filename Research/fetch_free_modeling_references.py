"""Archive freely published primary documentation; no paid course content."""
from pathlib import Path
import hashlib
import json
import urllib.request

out = Path('E:/TeslaRoadsterSim/Research/FreeModeling-20261026')
out.mkdir(parents=True, exist_ok=True)
pages = {
    'solidify': 'https://docs.blender.org/manual/en/5.1/modeling/modifiers/generate/solidify.html',
    'shrinkwrap': 'https://docs.blender.org/manual/en/5.1/modeling/modifiers/deform/shrinkwrap.html',
    'data_transfer': 'https://docs.blender.org/manual/en/5.1/modeling/modifiers/modify/data_transfer.html',
    'principled': 'https://docs.blender.org/manual/en/5.1/render/shader_nodes/shader/principled.html',
    'normals': 'https://docs.blender.org/manual/en/5.1/modeling/meshes/editing/mesh/normals.html',
    'cgmasters_free_pbr': 'https://cgmasters.com/principled-node-and-physically-based-rendering/',
}
records = []
for name, url in pages.items():
    record = {'name': name, 'url': url}
    try:
        request = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(request, timeout=30) as response:
            body = response.read()
            record['http_status'] = response.status
        (out / (name + '.html')).write_bytes(body)
        record.update(bytes=len(body), sha256=hashlib.sha256(body).hexdigest(), saved=str(out / (name + '.html')))
    except Exception as error:
        record['error'] = str(error)
    records.append(record)
(out / 'source_manifest.json').write_text(json.dumps(records, indent=2))
print(json.dumps([{k: v for k, v in r.items() if k in ('name','http_status','bytes','error')} for r in records]))
