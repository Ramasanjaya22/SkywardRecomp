"""Native settings regression checks; no game files or player settings are used."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    exe = directory / ('settings-test.exe' if os.name == 'nt' else 'settings-test')
    subprocess.run(shlex.split(os.environ.get('CC', 'clang')) +
                   ['-std=gnu11', '-Iruntime', 'runtime/settings.c', 'runtime/cosmetic_colour.c', 'tools/test_settings.c', '-o', str(exe), '-lm'],
                   cwd=ROOT, check=True)
    cases = [(setting, {}, code) for setting, code in
             [('zh-CN', 'zh-Hans'), ('zh-TW', 'zh-Hant'), ('zh-Hans', 'zh-Hans'),
              ('zh-Hant', 'zh-Hant'), ('en-US', 'en-US'), ('ja', 'ja'), ('unknown', 'en-US')]]
    if sys.platform.startswith('linux'):
        for locale in ['zh_CN.UTF-8', 'zh_SG.UTF-8', 'zh-Hans', 'zh-Hans-HK', 'zh_Hans_CN', 'zh']:
            cases.append(('auto', {'LC_ALL': locale}, 'zh-Hans'))
        for locale in ['zh_TW.UTF-8', 'zh_HK.UTF-8', 'zh_MO.UTF-8', 'zh-HK', 'zh-MO',
                       'zh-Hant', 'zh-Hant-TW', 'zh_Hant_CN', 'zh_hant', 'zh_hk.UTF-8']:
            cases.append(('auto', {'LC_ALL': locale}, 'zh-Hant'))
        cases += [('auto', {'LC_MESSAGES': 'zh_HK.UTF-8', 'LANG': 'zh_CN.UTF-8'}, 'zh-Hant'),
                  ('auto', {'LANG': 'zh_MO.UTF-8'}, 'zh-Hant'),
                  ('auto', {'LC_ALL': 'zh_CN.UTF-8', 'LC_MESSAGES': 'zh_HK.UTF-8'}, 'zh-Hans'),
                  ('auto', {'LC_ALL': 'ja_JP.UTF-8'}, 'ja'), ('auto', {'LC_ALL': 'C'}, 'en-US')]
    count = 0
    # free camera: the toggle and right-stick scale round-trip; out-of-range scales clamp to 0.25..2.0
    for lines, enabled, sens in [
        (['free_cam = 1'], '1', '1.00'),
        (['free_cam = 1', 'free_cam_sens = 1.50'], '1', '1.50'),
        (['free_cam = 1', 'free_cam_sens = 5.0'], '1', '2.00'),
        (['free_cam = 1', 'free_cam_sens = 0.01'], '1', '0.25'),
        (['free_cam = 0'], '0', '1.00'),
        (['free_cam = 1', 'free_cam_sens = nan'], '1', '1.00'),
    ]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        subprocess.run([str(exe), 'free-cam', enabled, sens], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True,
                       capture_output=True)
        saved = ini.read_text()
        for line in ('free_cam = ' + enabled, 'free_cam_sens = ' + sens):
            assert line + '\n' in saved, (lines, saved)
        count += 1
    # ZR sword jab: the toggle round-trips and stays off unless the file asks for it
    for lines, enabled in [(['zr_attack = 1'], '1'), (['zr_attack = 0'], '0'), ([], '0'), (['zr_attack = 2'], '1')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        subprocess.run([str(exe), 'zr-attack', enabled], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True,
                       capture_output=True)
        saved = ini.read_text()
        assert 'zr_attack = ' + enabled + '\n' in saved, (lines, saved)
        count += 1
    # Frame rate: each value round-trips; none or an unknown one is Auto (the game's own 60 / 30)
    for lines, fps, saved_as in [(['fps = 30'], '30', '30'), (['fps = 60'], '60', '60'), (['fps = 90'], '90', '90'),
                                 (['fps = 120'], '120', '120'), (['fps = unlocked'], '-1', 'unlocked'),
                                 (['fps = auto'], '0', 'auto'), ([], '0', 'auto'), (['fps = 45'], '0', 'auto')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        subprocess.run([str(exe), 'fps', fps], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True,
                       capture_output=True)
        saved = ini.read_text()
        assert 'fps = ' + saved_as + '\n' in saved, (lines, saved)
        count += 1
    # Screen filter: each key round-trips (Vivid = 4); none or an unknown one is Off
    for lines, index, saved_as in [(['screen_filter = grayscale'], '3', 'grayscale'), (['screen_filter = vivid'], '4', 'vivid'),
                                   (['screen_filter = gameboy'], '1', 'gameboy'), ([], '0', 'off'), (['screen_filter = neon'], '0', 'off')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        subprocess.run([str(exe), 'screen-filter', index], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True,
                       capture_output=True)
        saved = ini.read_text()
        assert 'screen_filter = ' + saved_as + '\n' in saved, (lines, saved)
        count += 1
    for newline in ['\n', '\r\n']:
        for setting, locale, code in cases:
            for seed in ['Regression seed', '']:
                ini = directory / 'test.ini'
                lines = [f'language = {setting}', 'aspect = original', 'upscaler = bilinear',
                         'prompts = xbox', 'volume = 1.25', 'volume =', 'language =', f'rando_seed = {seed}']
                ini.write_bytes((newline.join(lines) + newline).encode())
                env = dict(os.environ, LARECOMP_SETTINGS=str(ini), LC_ALL='', LC_MESSAGES='', LANG='C')
                env.update(locale)
                subprocess.run([str(exe), code, seed], env=env, check=True, capture_output=True)
                saved = ini.read_text()
                expected = 'auto' if setting in ['auto', 'unknown'] else code
                assert f'language = {expected}\n' in saved
                assert f'rando_seed = {seed}\n' in saved
                count += 1
    # the upscaler's default follows the GPU until the player picks one (settings_resolve_defaults)
    for lines, mode in [(['upscaler = auto'], 'upscaler-auto'), (['volume = 1'], 'upscaler-auto'),
                        (['upscaler = fsr1'], 'upscaler-chosen'), (['upscaler = metalfx'], 'upscaler-chosen')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        subprocess.run([str(exe), mode], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True, capture_output=True)
        if mode == 'upscaler-auto':
            assert 'upscaler = auto\n' in ini.read_text()
        count += 1
    # likewise the resolution: Match display until the player picks one (also on integrated GPUs); a saved one is kept
    for lines, mode, saved in [(['resolution = auto'], 'res-auto', 'auto'), (['volume = 1'], 'res-auto', 'auto'),
                               (['resolution = match'], 'res-chosen', 'match'), (['resolution = 1x'], 'res-chosen', '1x'),
                               (['resolution = 75%'], 'res-chosen', '75%'), (['resolution = dynamic'], 'res-chosen', 'dynamic')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        env = dict(os.environ, LARECOMP_SETTINGS=str(ini))
        env.pop('LARECOMP_INTEGRATED', None)
        subprocess.run([str(exe), mode], env=env, check=True, capture_output=True)
        assert f'resolution = {saved}\n' in ini.read_text(), (lines, ini.read_text())
        count += 1
    # Shadows (issue #602): Default unless the file says High, and written back the same way
    for lines, saved in [([], 'default'), (['shadows = default'], 'default'), (['shadows = high'], 'high'), (['shadows = ultra'], 'default')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(['resolution = 1x'] + lines) + '\n')
        subprocess.run([str(exe), 'res-chosen'], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True, capture_output=True)
        assert f'shadows = {saved}\n' in ini.read_text(), (lines, ini.read_text())
        count += 1
    # Cosmetics colour rows (shared palette): older names map onto it, Custom is a hex value, unknown -> Default
    ini = directory / 'test.ini'
    ini.write_text('tunic = Yellow\nhearts = Pink\nhair = #12ab34\nshield = Sparkly\nskin = Random\n')
    subprocess.run([str(exe), 'colours'], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True, capture_output=True)
    saved = ini.read_text()
    for line in ['tunic = Gold', 'hearts = Pink', 'hair = #12AB34', 'shield = Default', 'skin = Random']:
        assert line + '\n' in saved, (line, saved)
    count += 1
    # the Randomizer tab's New seed options: an older file without the key stays without it (the defaults)
    for lines, value in [(['rando_options = 50000-00004-20000-40G20-04000-0G11R', 'rando_seed = X'],
                          '50000-00004-20000-40G20-04000-0G11R'), (['rando_seed = X'], '')]:
        ini = directory / 'test.ini'
        ini.write_text('\n'.join(lines) + '\n')
        subprocess.run([str(exe), 'rando-options', value], env=dict(os.environ, LARECOMP_SETTINGS=str(ini)), check=True,
                       capture_output=True)
        saved = ini.read_text()
        assert (f'rando_options = {value}\n' in saved) if value else 'rando_options' not in saved, saved
        count += 1
    print(f'{count} parser, locale, upscaler / resolution default and roundtrip cases passed (LF/CRLF, empty/nonempty seeds)')
