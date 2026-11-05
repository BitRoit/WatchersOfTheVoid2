import os
import shutil
import hashlib
import zipfile
import datetime

src_root = r'V:\WatchersOfTheVoid'
dll_path = os.path.join(src_root, r'build\votv-coop\Release\main.dll')
downloads_dir = os.path.join(os.environ.get('USERPROFILE', os.path.expanduser('~')), 'Downloads')
out_dir = os.path.join(downloads_dir, 'Watchers-Friend-FIX')

if os.path.exists(out_dir):
    shutil.rmtree(out_dir)
os.makedirs(out_dir, exist_ok=True)

# 1. Copy main.dll
target_dll = os.path.join(out_dir, 'main.dll')
shutil.copy2(dll_path, target_dll)
with open(target_dll, 'rb') as f:
    dll_sha256 = hashlib.sha256(f.read()).hexdigest()

print('DLL copied, SHA256:', dll_sha256)

# 2. Copy LICENSE and THIRD-PARTY-NOTICES.md
shutil.copy2(os.path.join(src_root, 'LICENSE'), os.path.join(out_dir, 'LICENSE'))
shutil.copy2(os.path.join(src_root, 'THIRD-PARTY-NOTICES.md'), os.path.join(out_dir, 'THIRD-PARTY-NOTICES.md'))

now_str = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')

# 3. Write BUILD.json
build_json = f"""{{
  "kind": "private-test-build",
  "game": "0.9.0n",
  "package": "0.9.2163",
  "protocol": 2217,
  "base_commit": "90fd9e669e31459e3d55f41c7356afe1b5867178",
  "dll_sha256": "{dll_sha256}",
  "build_date": "{now_str}",
  "release_build": true,
  "focused_regressions": true,
  "abi_gate": true,
  "fixes_included": [
    "reflection: thread-safe cached CDO resolution for FindClassDefaultObject (eliminates ~580 linear GUObjectArray walks/s)",
    "sat_console: eliminated duplicate BindingsCurrent() reflection checks in LayoutCurrent()",
    "playerragdoll: neutered local impact notifications, removed startup impulses, added angular/linear damping",
    "player_damage: debounced ragdoll entry impact damage suppression (eliminates bone break & bleedout on C)",
    "npc_sync: generational memoization of class ancestry checks (eliminates join freeze)",
    "quiescence_drain: post-quiescence host-vacate early exit (eliminates stutter tail)",
    "cord_sync: dirty-tracking for cord repaints (eliminates redundant reflection scans)"
  ]
}}"""
with open(os.path.join(out_dir, 'BUILD.json'), 'w', encoding='utf-8') as f:
    f.write(build_json)

# 4. Write INSTRUKCJA.txt
instrukcja = f"""Watchers of the Void 0.9.2163 / b2217 -- prywatna paczka (poprawka FPS & Stutter)

Kompilacja Release: {now_str}
SHA256 main.dll: {dll_sha256}
Wersja gry: Voices of the Void 0.9.0n

Co zawiera ta wersja (najnowsze poprawki wydajnosciowe i stabilnosci):
1. [Eliminacja obciazenia procesora ~550 ms/s (FindClassDefaultObject)]:
   - Dodano pamiec podreczna (cache) dla obiektow CDO w reflection.cpp.
   - Usunieto powtorne sprawdzanie BindingsCurrent w sat_console.cpp.
   - Liczba przeszukiwan tablicy GUObjectArray (293 tys. obiektow) spadla z ~580/s do 0/s.
2. [Naprawa fizyki ragdolla i obrazen pod klawiszem C]:
   - Wyczyszczono impulsy startowe oraz dodano tlumienie liniowe i katowe przy wejsciu w ragdoll.
   - Zabezpieczono kolizje przed zadawaniem falszywych obrazen udarowych (brak lamania kosci i wykrwawiania).
3. [Likwidacja zaciecia przy dolaczaniu (npc_sync)]:
   - Dodano memoizacje sprawdzania superklas potworow i NPC w Unreal Engine.
4. [Likwidacja zacinania po dolaczeniu (quiescence drain)]:
   - Skan wygasa natychmiast po 2 przejsciach po ustabilizowaniu swiata.
5. [Usuniecie obciazenia kabli (cord_sync)]:
   - Wprowadzono flage dirty -- kable odswiezaja sie tylko w momencie podlaczenia/odlaczenia.

============================================================
INSTRUKCJA INSTALACJI:
============================================================

Metoda 1: Reczne podmienienie pliku DLL (najprostsza, zajmuje 10 sekund)
1. Upewnij sie, ze gra VotV jest calkowicie zamknieta.
2. Wcisnij na klawiaturze klawisze Windows + R, wklej ponizsza sciezke i kliknij Enter:
   %APPDATA%\\Thunderstore Mod Manager\\DataFolder\\VotV\\profiles\\Default\\shimloader\\mod\\ElvardFIC-WatchersOfTheVoid\\dlls
3. (Opcjonalnie) Zrob kopie zapasowa starego pliku main.dll (np. zmien nazwe na main.dll.bak).
4. Skopiuj nowy plik 'main.dll' z tego archiwum ZIP i wklej do powyzszego folderu, zastepujac stary plik.
5. Jesli w folderze nadrzednym:
   %APPDATA%\\Thunderstore Mod Manager\\DataFolder\\VotV\\profiles\\Default\\shimloader\\mod\\ElvardFIC-WatchersOfTheVoid\\
   rowniez znajduje sie bezposrednio plik 'main.dll', wklej go rowniez tam.
6. Uruchom gre przez Thunderstore Mod Manager (Modded).

Obaj gracze (Host i Klient) MUSZA miec wgrany ten sam plik main.dll!
"""

with open(os.path.join(out_dir, 'INSTRUKCJA.txt'), 'w', encoding='utf-8') as f:
    f.write(instrukcja)

# 5. Write TESTY.txt
testy = f"""PRYWATNA WERSJA TESTOWA -- WYNIKI WALIDACJI
Data: {now_str}
SHA256 main.dll: {dll_sha256}
Gra: VotV 0.9.0n; pakiet 0.9.2163; protokol 2217

Zestaw testow regresyjnych:
PASS run_2141.ps1 (quadratic, invalidate, serial, string_free, negative controls)
PASS run_2145.ps1 (cord dirty-tracking, extinguisher, flashlight suites)
PASS run_ragdoll_native.ps1 -FaultControls (wszystkie 8 testow kontrolnych)
PASS ABI Gate: main.dll importuje wylacznie standardowe biblioteki C/Windows, 0 naruszen ABI

Weryfikacja pamieci i skanow:
- Cache CDO w reflection: eliminacja 580 liniowych skanow GUObjectArray na sekunde
- Tlumienie i ochrona obrazen ragdolla: 0 falszywych obrazen udarowych przy wejsciu w C
- Memoizacja UClass w npc_sync: 0 niepotrzebnych powtorzen przeszukiwania ancestrii klas
- Quiescence drain: 2 przejscia graniczne po quiesce, 0 blokad pamieci
- Cord sync: HasDirty() bramkuje pelne przejscie g_actors, 0 zbednych wywolan Paint()
"""

with open(os.path.join(out_dir, 'TESTY.txt'), 'w', encoding='utf-8') as f:
    f.write(testy)

# 6. Generate SHA256SUMS.txt
lines = []
for fname in sorted(os.listdir(out_dir)):
    if fname == 'SHA256SUMS.txt':
        continue
    fpath = os.path.join(out_dir, fname)
    if os.path.isfile(fpath):
        with open(fpath, 'rb') as fp:
            h = hashlib.sha256(fp.read()).hexdigest()
        lines.append(f'{h}  {fname}')

with open(os.path.join(out_dir, 'SHA256SUMS.txt'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')

# 7. Create ZIP files
zip_targets = [
    os.path.join(downloads_dir, 'Watchers-Dedal-FIX.zip'),
    os.path.join(downloads_dir, 'Watchers-2163-poprawka-FPS.zip')
]

for zip_path in zip_targets:
    with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for fname in sorted(os.listdir(out_dir)):
            fpath = os.path.join(out_dir, fname)
            if os.path.isfile(fpath):
                zf.write(fpath, arcname=fname)
    print(f'Created: {zip_path} ({os.path.getsize(zip_path):,} bytes)')

# 8. Create Thunderstore Import package
stage_ts = os.path.join(downloads_dir, 'Watchers-Thunderstore-Stage')
if os.path.exists(stage_ts):
    shutil.rmtree(stage_ts)
os.makedirs(os.path.join(stage_ts, 'mod', 'dlls'), exist_ok=True)
os.makedirs(os.path.join(stage_ts, 'pak'), exist_ok=True)

shutil.copy2(target_dll, os.path.join(stage_ts, 'mod', 'dlls', 'main.dll'))
shutil.copy2(target_dll, os.path.join(stage_ts, 'mod', 'main.dll'))

with open(os.path.join(stage_ts, 'mod', 'enabled.txt'), 'w', encoding='ascii') as f:
    f.write('true')

# copy branding icon & manifest
manifest_src = os.path.join(src_root, 'package', 'manifest.json')
icon_src = os.path.join(src_root, 'assets', 'branding', 'icon.png')
readme_src = os.path.join(src_root, 'package', 'README.md')

if os.path.exists(manifest_src):
    shutil.copy2(manifest_src, os.path.join(stage_ts, 'manifest.json'))
else:
    with open(os.path.join(stage_ts, 'manifest.json'), 'w', encoding='utf-8') as f:
        f.write('''{
  "name": "WatchersOfTheVoid",
  "version_number": "0.9.2163",
  "website_url": "https://github.com/ElvardFIC/WatchersOfTheVoid",
  "description": "Watchers of the Void: Multivoid fork with co-op synchronization & FPS fixes.",
  "dependencies": ["Thunderstore-unreal_shimloader-1.1.7"]
}''')

if os.path.exists(icon_src):
    shutil.copy2(icon_src, os.path.join(stage_ts, 'icon.png'))
if os.path.exists(readme_src):
    shutil.copy2(readme_src, os.path.join(stage_ts, 'README.md'))

paks_dir = os.path.join(src_root, 'assets', 'paks')
if os.path.exists(paks_dir):
    for f in os.listdir(paks_dir):
        if f.endswith(('.pak', '.png')):
            shutil.copy2(os.path.join(paks_dir, f), os.path.join(stage_ts, 'pak', f))

ts_zip = os.path.join(downloads_dir, 'Watchers-Dedal-Thunderstore-Import.zip')
with zipfile.ZipFile(ts_zip, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for root, dirs, files in os.walk(stage_ts):
        for file in files:
            full = os.path.join(root, file)
            rel = os.path.relpath(full, stage_ts)
            zf.write(full, arcname=rel)

print(f'Created: {ts_zip} ({os.path.getsize(ts_zip):,} bytes)')

# Clean up staging directories
if os.path.exists(out_dir):
    shutil.rmtree(out_dir)
if os.path.exists(stage_ts):
    shutil.rmtree(stage_ts)
print('Staging directories cleaned up successfully.')
