#!/usr/bin/env python3
"""Capture reproducible route inputs, raw responses and separate route/ETA metrics."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import urlsplit, urlunsplit


class ServiceBusy(RuntimeError):
    pass


def endpoint(value):
    url = urlsplit(value)
    if url.scheme != 'https' or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError('Use an HTTPS service root without credentials, query or fragment')
    path = url.path.rstrip('/')
    if path.rsplit('/', 1)[-1] in ('route', 'status', 'isochrone'):
        path = path.rsplit('/', 1)[0]
    return urlunsplit((url.scheme, url.netloc, path, '', ''))


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')


def source_hashes():
    """Include new, untracked adapter files that a git diff cannot describe."""
    root = Path(__file__).resolve().parents[3]
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard',
        '--', 'libs/routing', 'web/src/routing.js', 'web/scripts/inspect-valhalla.mjs',
        'tools/routing/valhalla', 'data/test_data/valhalla/route.json'], cwd=root).decode().split('\0')
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in sorted(set(paths)) if name and (root / name).is_file()}


def request(points, avoid):
    return {'locations': [{'lat': p[0], 'lon': p[1], 'type': 'break'} for p in points],
            'costing': 'auto', 'costing_options': {'auto': {'use_tolls': 0 if avoid else 0.5,
              'use_highways': 0.5, 'use_ferry': 0.5}}, 'units': 'kilometers',
            'language': 'fr-FR', 'shape_format': 'polyline6'}


def fetch(url, destination, payload=None):
    args = ['curl', '--silent', '--show-error', '--fail-with-body', '--max-time', '30',
            '--proto', '=https', '--write-out', '%{http_code}', '-H', 'X-Client-Id: comaps-photon', '-o', str(destination)]
    if payload:
        args += ['-H', 'Content-Type: application/json', '--data-binary', '@' + str(payload)]
    result = subprocess.run(args + [url], capture_output=True, text=True)
    if result.stdout.strip() in ('429', '503'):
        raise ServiceBusy('Service is busy; stop this run instead of retrying the remaining cases')
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f'curl exited {result.returncode}')
    return json.loads(destination.read_text())


def metrics(data, avoid):
    trip = data.get('trip', {})
    summary = trip.get('summary', {})
    legs = trip.get('legs', [])
    toll_values = [summary.get('has_toll')] + [leg.get('summary', {}).get('has_toll') for leg in legs]
    toll = any(x is True for x in toll_values) or any(m.get('toll') is True for leg in legs for m in leg.get('maneuvers', []))
    known = bool(legs) and all(type(x) is bool for x in toll_values)
    warnings = data.get('warnings', []) or trip.get('warnings', []) or any(leg.get('warnings') for leg in legs)
    valid = (trip.get('status') == 0 and trip.get('units') == 'kilometers' and bool(legs)
             and isinstance(summary.get('length'), (int, float)) and isinstance(summary.get('time'), (int, float)))
    return {'accepted': bool(valid and not warnings and (not avoid or (known and not toll))),
            'seconds': summary.get('time'), 'meters': summary.get('length', 0) * 1000,
            'has_toll': toll if known else None, 'warnings': warnings}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--endpoint', required=True)
    parser.add_argument('--expected-version', required=True, help='Exact /status version; abort on drift')
    parser.add_argument('--cases', type=Path, default=Path(__file__).with_name('cases.json'))
    parser.add_argument('--case', default='lansuscle-bordeaux')
    parser.add_argument('--all', action='store_true', help='Explicitly request the full 20-journey set')
    parser.add_argument('--output', required=True, type=Path, help='New output directory; never overwrite a run')
    parser.add_argument('--interval', type=float, default=2, help='Minimum seconds between requests (at least 2)')
    parser.add_argument('--comaps-tool', type=Path)
    parser.add_argument('--maps', type=Path, help='Dedicated benchmark copy of downloaded MWM maps')
    parser.add_argument('--resources', type=Path)
    args = parser.parse_args()
    root = endpoint(args.endpoint)
    cases = json.loads(args.cases.read_text())
    if not args.all:
        cases = [c for c in cases if c['id'] == args.case]
    if not cases:
        parser.error('Unknown case')
    if args.comaps_tool and (not args.maps or not args.resources):
        parser.error('--comaps-tool requires --maps and --resources')
    args.output.mkdir(parents=True, exist_ok=False)
    write(args.output / 'cases.json', cases)
    status = fetch(root + '/status', args.output / 'status.json')
    if status.get('version') != args.expected_version:
        raise RuntimeError(f"Version mismatch: expected {args.expected_version}, got {status.get('version')}")
    metadata = {'captured_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'endpoint': root, 'valhalla_status': status,
                'comaps_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                'comaps_worktree_diff_sha256': hashlib.sha256(subprocess.check_output(['git', 'diff', 'HEAD'])).hexdigest(),
                'source_sha256': source_hashes(),
                'quality_gate': 'pending_independent_review', 'comaps': 'not_run'}
    records = []
    write(args.output / 'metadata.json', metadata)
    for case in cases:
        for avoid in (False, True):
            name = case['id'] + ('-avoid-tolls' if avoid else '-allow-tolls')
            payload = args.output / (name + '.request.json')
            response_path = args.output / (name + '.response.json')
            write(payload, request(case['points'], avoid))
            case_path = args.output / (case['id'] + '.case.json')
            write(case_path, case)
            time.sleep(max(2, args.interval))
            start = time.monotonic()
            record = {'id': case['id'], 'avoid_tolls': avoid, 'provider': 'valhalla'}
            try:
                data = fetch(root + '/route', response_path, payload)
                record['latency_seconds'] = time.monotonic() - start
                record.update(metrics(data, avoid))
                inspector = Path(__file__).resolve().parents[3] / 'web/scripts/inspect-valhalla.mjs'
                inspection = subprocess.check_output(['node', str(inspector), str(response_path),
                    str(args.output / (name + '.geojson')), str(avoid).lower(), str(case_path)], text=True)
                record.update(json.loads(inspection))
                record['accepted'] = record['accepted'] and record['adapter_accepted']
                record['response_sha256'] = hashlib.sha256(response_path.read_bytes()).hexdigest()
            except ServiceBusy:
                write(args.output / 'results.json', records)
                raise
            except (RuntimeError, ValueError, subprocess.CalledProcessError) as error:
                record.update(accepted=False, error=str(error))
            record.setdefault('latency_seconds', time.monotonic() - start)
            records.append(record)
            write(args.output / 'results.json', records)
            print(json.dumps(record), flush=True)
    final_status = fetch(root + '/status', args.output / 'status-after.json')
    metadata['dataset_stable'] = all(final_status.get(k) == status.get(k) for k in ('version', 'tileset_last_modified'))
    if args.comaps_tool:
        maps = list(args.maps.rglob('*.mwm'))
        if not maps:
            raise RuntimeError('No MWM maps found; CoMaps comparison cannot run')
        metadata['map_hashes'] = {}
        for path in maps:
            with path.open('rb') as stream:
                metadata['map_hashes'][str(path.relative_to(args.maps))] = hashlib.file_digest(stream, 'sha256').hexdigest()
        for avoid in (False, True):
            for no_leaps in (False, True):
                name = f'comaps-tolls-{avoid}-no-leaps-{no_leaps}.json'
                subprocess.run([str(args.comaps_tool.resolve()), '--cases=' + str((args.output / 'cases.json').resolve()),
                    '--output=' + str((args.output / name).resolve()), '--maps=' + str(args.maps.resolve()),
                    '--resources=' + str(args.resources.resolve()), '--avoid_tolls=' + str(avoid).lower(),
                    '--no_leaps=' + str(no_leaps).lower()], check=True, timeout=120 * len(cases) + 60)
        metadata['comaps'] = 'captured_pending_review'
    write(args.output / 'metadata.json', metadata)
    if not metadata['dataset_stable']:
        raise RuntimeError('Server dataset changed during capture; rerun before comparing')


if __name__ == '__main__':
    main()
