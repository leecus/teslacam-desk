"""Generate synthetic TeslaCam footage for local manual verification; never touches a real drive."""
import json
import shutil
import subprocess
from pathlib import Path

project = Path(__file__).resolve().parents[1]
verification = project / '.verification'
root = verification / '演示 U盘' / 'TeslaCam'
root.mkdir(parents=True, exist_ok=True)
ffmpeg = project / 'bin' / 'ffmpeg.exe'
cameras = ['front', 'back', 'left_repeater', 'right_repeater']
templates = {}
for index, camera in enumerate(cameras):
    target = verification / f'demo-{camera}.mp4'
    if not target.exists():
        subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-n', '-f', 'lavfi',
                        '-i', 'testsrc2=size=640x360:rate=15', '-vf', f'hue=h={index*60}',
                        '-t', '60', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '32',
                        '-pix_fmt', 'yuv420p', str(target)], check=True)
    templates[camera] = target
events = [
    ('SentryClips/2026-11-12_14-00-00', '2026-11-12_14-00-00'),
    ('SentryClips/2026-11-12_14-00-00', '2026-11-12_14-01-00'),
    ('SavedClips/2026-11-13_09-30-00', '2026-11-13_09-30-00'),
    ('RecentClips', '2026-11-15_10-01-00'),
    ('RecentClips', '2026-11-15_10-11-00'),
]
for directory, timestamp in events:
    folder = root / directory
    folder.mkdir(parents=True, exist_ok=True)
    for camera, template in templates.items():
        target = folder / f'{timestamp}-{camera}.mp4'
        if not target.exists():
            shutil.copy2(template, target)
metadata = root / 'SentryClips/2026-11-12_14-00-00/event.json'
if not metadata.exists():
    metadata.write_text(json.dumps({'timestamp': '2026-11-12T14:00:45+08:00', 'reason': 'synthetic demo'}), encoding='utf-8')
print(root)
