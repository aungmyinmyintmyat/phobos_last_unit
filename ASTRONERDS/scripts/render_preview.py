#!/usr/bin/env python3
"""Capture actual C++ frames and package GIF/PNG previews (optional Pillow tool)."""
import argparse
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/horizon')
    parser.add_argument('--output', default='build/previews')
    parser.add_argument('--ship-only', action='store_true', help='Capture the overhead rescue-ship interior')
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    frames_path = output / 'frames'
    if args.ship_only:
        subprocess.run([str(Path(args.binary).resolve()), '--ship-preview', str(frames_path)], check=True)
        frames = []
        for filename in (frames_path / 'ship-frames.tsv').read_text().splitlines():
            with Image.open(frames_path / filename) as frame:
                frames.append(frame.convert('RGB').resize((1280, 720), Image.Resampling.NEAREST))
        frames[0].save(output / 'ship-interior.png')
        for room in ('airlock', 'blocked-forward-room', 'engine', 'monitor', 'monitor-tvs', 'pc-setup', 'cargo', 'storage', 'crew'):
            with Image.open(frames_path / f'ship-{room}.ppm') as frame:
                frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'ship-{room}.png')
        frames[0].save(output / 'ship-interior.gif', save_all=True, append_images=frames[1:],
                       duration=83, loop=0, optimize=False, disposal=2)
        for clip in ('monitor-tvs', 'cargo-rover'):
            detail_frames = []
            for filename in (frames_path / f'{clip}-frames.tsv').read_text().splitlines():
                with Image.open(frames_path / filename) as frame:
                    detail_frames.append(frame.convert('RGB').resize((1280, 720), Image.Resampling.NEAREST))
            detail_frames[0].save(output / f'{clip}.gif', save_all=True, append_images=detail_frames[1:],
                                   duration=83, loop=0, optimize=False, disposal=2)
        for room in ('cargo', 'monitor'):
            door_frames = []
            for filename in (frames_path / f'{room}-door-frames.tsv').read_text().splitlines():
                with Image.open(frames_path / filename) as frame:
                    door_frames.append(frame.convert('RGB').resize((1280, 720), Image.Resampling.NEAREST))
            door_frames[0].save(output / f'{room}-door.gif', save_all=True, append_images=door_frames[1:],
                                duration=83, loop=0, optimize=False, disposal=2)
            for state in ('closed', 'entered'):
                with Image.open(frames_path / f'{room}-door-{state}.ppm') as frame:
                    frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{room}-door-{state}.png')
        print(f'Ship previews saved in {output.resolve()}')
        return
    subprocess.run([str(Path(args.binary).resolve()), '--preview', str(frames_path)], check=True)
    clips = {}
    # The manifest, rather than a directory glob, excludes stale frames from older runs.
    for row in (frames_path / 'frames.tsv').read_text().splitlines():
        stage, filename, _ = row.split('\t')
        with Image.open(frames_path / filename) as frame:
            clips.setdefault(stage, []).append(frame.convert('RGB').copy())
    for stage in ('jump', 'movement', 'mining', 'crafting', 'climb', 'launch'):
        frames = clips[stage]
        frames[0].save(output / f'{stage}.gif', save_all=True, append_images=frames[1:],
                       duration=50, loop=0, optimize=False, disposal=2)
    selected = {
        'overview': clips['overview'][0],
        'mining': clips['mining'][min(20, len(clips['mining']) - 1)],
        'crafting': clips['crafting'][min(15, len(clips['crafting']) - 1)],
        'tasks': clips['tasks'][-1],
    }
    clips['jump'][min(9, len(clips['jump'])-1)].resize((1280,720),Image.Resampling.NEAREST).save(output / 'jump.png')
    for name in ('craft-menu', 'materials', 'guide', 'task-panel', 'inspection', 'lamp-off'):
        with Image.open(frames_path / f'{name}.ppm') as frame:
            frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
            if name == 'craft-menu':
                selected['crafting'] = frame.convert('RGB').copy()
            if name == 'task-panel':
                selected['tasks'] = frame.convert('RGB').copy()
    for name, frame in selected.items():
        frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
    model_comparison = Image.new('RGB', (1000, 480), '#08111e')
    model_labels = ('MARK 1 / ORIGINAL', 'MARK 2 / EXPLORER')
    for index, name in enumerate(('robot-mark1', 'robot-mark2')):
        with Image.open(frames_path / f'{name}.ppm') as frame:
            actual = frame.convert('RGB')
            actual.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
            # Identical camera, pose and world; magnify native pixels without smoothing.
            detail = actual.crop((282, 156, 358, 224)).resize((456, 408), Image.Resampling.NEAREST)
            detail.save(output / f'{name}-detail.png')
            x = 22 + index * 500
            model_comparison.paste(detail, (x, 48))
            ImageDraw.Draw(model_comparison).text((x, 20), model_labels[index], fill='#9adfc9')
    model_comparison.save(output / 'robot-models.png')
    for name in ('camp-exterior', 'camp-interior', 'camp-workbench'):
        with Image.open(frames_path / f'{name}.ppm') as frame:
            frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
    survival_frames = {}
    for name in ('daylight', 'night', 'warning', 'danger', 'charging', 'battery-empty'):
        with Image.open(frames_path / f'{name}.ppm') as frame:
            actual = frame.convert('RGB').copy()
            actual.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
            survival_frames[name] = actual
    survival_sheet = Image.new('RGB', (1280, 1152), '#08111e')
    survival_labels = ('NOON / 60X PHOBOS LIGHT CYCLE', 'MIDNIGHT / EYE BEAMS ON',
                       'WARNING / COUNTDOWN TO SHELTER', 'DANGER / FICTIONAL CREATURES',
                       'BASE CAMP / BATTERY RECHARGING', 'BATTERY EMPTY / MISSION FAILED')
    for index, ((_, frame), caption) in enumerate(zip(survival_frames.items(), survival_labels)):
        x, y = index % 2 * 640, index // 2 * 384
        ImageDraw.Draw(survival_sheet).text((x+12, y+7), caption, fill='#9adfc9')
        survival_sheet.paste(frame, (x, y+24))
    survival_sheet.save(output / 'survival-contact-sheet.png')
    with Image.open(frames_path / 'warning-menu.ppm') as frame:
        frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / 'warning-menu.png')
    alarm_frames = []
    for filename in (frames_path / 'alarm-frames.tsv').read_text().splitlines():
        with Image.open(frames_path / filename) as frame:
            alarm_frames.append(frame.convert('RGB').copy())
    alarm_frames[0].save(output / 'danger-alarm.gif', save_all=True, append_images=alarm_frames[1:],
                         duration=250, loop=0, optimize=False, disposal=2)
    camp_frames = []
    for filename in (frames_path / 'camp-frames.tsv').read_text().splitlines():
        with Image.open(frames_path / filename) as frame:
            camp_frames.append(frame.convert('RGB').copy())
    camp_frames[0].save(output / 'camp-walkthrough.gif', save_all=True, append_images=camp_frames[1:],
                         duration=83, loop=0, optimize=False, disposal=2)
    astronaut_frames = []
    for filename in (frames_path / 'astronaut-frames.tsv').read_text().splitlines():
        with Image.open(frames_path / filename) as frame:
            # Keep the native pixel character crisp while making the breathing,
            # tapping hand and remote signal easy to inspect.
            astronaut_frames.append(frame.convert('RGB').crop((378, 158, 470, 232)).resize(
                (552, 444), Image.Resampling.NEAREST))
    astronaut_frames[0].save(output / 'astronaut-remote.gif', save_all=True,
                              append_images=astronaut_frames[1:], duration=50, loop=0,
                              optimize=False, disposal=2)
    collapse_frames = []
    for filename in (frames_path / 'collapse-frames.tsv').read_text().splitlines():
        with Image.open(frames_path / filename) as frame:
            collapse_frames.append(frame.convert('RGB').crop((268, 168, 352, 252)).resize(
                (504, 504), Image.Resampling.NEAREST))
    collapse_frames[0].save(output / 'collapse-damage.gif', save_all=True,
                             append_images=collapse_frames[1:], duration=34, loop=0,
                             optimize=False, disposal=2)
    for name in ('eye-night-on', 'eye-night-off'):
        with Image.open(frames_path / f'{name}.ppm') as frame:
            frame.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
    eye_jump = []
    for filename in (frames_path / 'eye-jump-frames.tsv').read_text().splitlines():
        with Image.open(frames_path / filename) as frame:
            eye_jump.append(frame.convert('RGB').copy())
    eye_jump[0].save(output / 'eye-jump-lighting.gif', save_all=True,
                     append_images=eye_jump[1:], duration=50, loop=0,
                     optimize=False, disposal=2)
    with Image.open(frames_path / 'lamp-off.ppm') as unlit:
        clips['overview'][0].save(output / 'lighting.gif', save_all=True,
                                  append_images=[unlit.convert('RGB')], duration=900, loop=0, disposal=2)
    trail_frames = []
    for name, elapsed in (('tread-fresh', 0), ('tread-fading', 15), ('tread-gone', 30)):
        with Image.open(frames_path / f'{name}.ppm') as frame:
            actual = frame.convert('RGB')
            actual.resize((1280, 720), Image.Resampling.NEAREST).save(output / f'{name}.png')
            captioned = Image.new('RGB', (640, 384), '#08111e')
            captioned.paste(actual, (0, 24))
            ImageDraw.Draw(captioned).text(
                (12, 7), f'TREAD FADE / +{elapsed:02d}s after stopping / time compressed', fill='#9adfc9')
            trail_frames.append(captioned)
    trail_frames[0].save(output / 'tread-fade.gif', save_all=True, append_images=trail_frames[1:],
                         duration=1100, loop=0, optimize=False, disposal=2)
    overview = Image.new('RGB', (1280, 768), '#08111e')
    draw = ImageDraw.Draw(overview)
    titles = ('01 / MISSION MARKERS', '02 / MINING LASER', '03 / CRAFTING CYCLE', '04 / TASK PROGRESS')
    for i, ((_, frame), title) in enumerate(zip(selected.items(), titles)):
        x, y = (i % 2) * 640, (i // 2) * 384
        draw.text((x + 12, y + 7), title, fill='#9adfc9')
        overview.paste(frame, (x, y + 24))
    overview.save(output / 'contact-sheet.png')
    combined = []
    for stage in ('jump', 'movement', 'mining', 'crafting', 'tasks', 'climb', 'launch'):
        combined.extend(clips[stage])
    combined[0].save(output / 'mission-preview.gif', save_all=True, append_images=combined[1:],
                     duration=50, loop=0, optimize=False, disposal=2)
    print(f'Previews saved in {output.resolve()}')


if __name__ == '__main__':
    main()
