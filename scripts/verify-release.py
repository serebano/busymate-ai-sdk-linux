from pathlib import Path
import hashlib,json,re
root=Path(__file__).resolve().parents[1]
m=json.loads((root/'release-manifest.json').read_text())
for name,expected in m['files'].items():
    actual=hashlib.sha256((root/name).read_bytes()).hexdigest()
    assert actual==expected, f"Frozen source changed: {name}"
    print(f"verified {name} {actual}")
assert m['version']=='1.0.1'

# The distribution version is declared once more in meson.build; both must agree.
meson=(root/'meson.build').read_text()
declared=re.search(r"project\('busymate-ai-sdk-linux',\s*'c',\s*version:\s*'([^']+)'",meson)
assert declared and declared.group(1)==m['version'], 'meson.build version differs from release-manifest.json'
print(f"verified meson.build version {declared.group(1)}")

# Wire constants in the public header must match the manifest.
core=(root/'src/busymate/busymate-core.h').read_text()
assert f'#define BUSYMATE_BRIDGE_WIRE {m["identityWire"]["v2"]}' in core
assert f'#define BUSYMATE_BRIDGE_BUILD "{m["identityWire"]["v2Build"]}"' in core
assert f'#define BUSYMATE_MICROPHONE_WIRE {m["microphoneWire"]}' in core
assert f'#define BUSYMATE_MICROPHONE_ADAPTER "{m["microphoneAdapter"]}"' in core
assert f'channel={m["chatChannel"]}' in core
print('verified wire constants and chat channel')
