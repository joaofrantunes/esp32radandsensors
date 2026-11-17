"""Safe, additive deployment of the ESP32 station backend. Python 3.9+ stdlib."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
TABLES = ('ESP32GEIGERSTATS', 'ESP32DEVICE', 'ESP32EXTRA')
PALETTES = ('node-red-dashboard', 'node-red-contrib-stackhero-mysql')
CONFIG_TYPES = ('mqtt-broker', 'Stackhero-MySQL-Server')


class SetupError(Exception):
    pass


def run(args, *, data=None, output=None):
    result = subprocess.run(args, input=data, stdout=output if output is not None else subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode:
        # Do not expose SQL, client option files, credentials or private paths from stderr.
        raise SetupError(f'{Path(args[0]).name} failed (exit {result.returncode}); check local service/account permissions')
    return result.stdout.decode('utf-8', errors='replace') if output is None else ''


def read_flow(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(data, list) or any(not isinstance(n, dict) or not isinstance(n.get('id'), str) for n in data):
        raise SetupError('Not a standard Node-RED array flow file')
    if len({n['id'] for n in data}) != len(data):
        raise SetupError('Duplicate node IDs; resolve manually before migration')
    return data


def merge_flow(current, desired, *, fresh=False):
    """Preserve all unrelated nodes and existing credentials/config node IDs."""
    old = {n['id']: n for n in current}
    new = {n['id']: n for n in desired}
    anchor = next(n for n in desired if n.get('type') == 'tab' and n.get('label') == 'ESP32 radiation and sensors')
    tabs = {n['id'] for n in desired if n.get('type') == 'tab'}
    if not fresh and old.get(anchor['id'], {}).get('type') != 'tab':
        raise SetupError('Existing project ID not recognized; use manual migration, never guess by name')
    if fresh and any(t in old for t in tabs):
        raise SetupError('Project already exists; use update instead of install')
    config_ids = {n['id'] for n in desired if n.get('type') in CONFIG_TYPES}
    if not fresh and not config_ids.issubset(old):
        raise SetupError('Original broker/MySQL config IDs missing; manual migration required')
    topics = {n.get('topic') for n in desired if n.get('type') == 'mqtt in'}
    def overlaps(subscription):
        if not isinstance(subscription, str):
            return False
        pattern = subscription.split('/')
        for topic in topics:
            parts = topic.split('/')
            if all(p == '#' or (i < len(parts) and (p == '+' or p == parts[i]))
                   for i, p in enumerate(pattern)) and (pattern[-1] == '#' or len(parts) == len(pattern)):
                return True
        return False
    for n in current:
        ident = n['id']
        if ident in new:
            expected = new[ident]
            if n.get('type') != expected.get('type') or n.get('z') != expected.get('z'):
                raise SetupError('Node ID/type/scope collision; no changes made')
            if n.get('type') == 'mqtt in' and (n.get('topic') != expected.get('topic') or n.get('broker') != expected.get('broker')):
                raise SetupError('Project MQTT topic or broker ID customized; manual migration required')
            if n.get('type') == 'Stackhero-MySQL' and n.get('server') != expected.get('server'):
                raise SetupError('Project MySQL server ID customized; manual migration required')
        elif n.get('type') == 'mqtt in' and overlaps(n.get('topic')):
            raise SetupError('Unrecognized subscription to a project topic; possible duplicate INSERT')
        elif n.get('z') in tabs:
            raise SetupError('Custom nodes in project tab; manual merge required to preserve custom wiring')
    if fresh and any(i in old for i in new):
        raise SetupError('Install IDs overlap existing flow; use manual migration')
    merged = []
    for n in current:
        ident = n['id']
        if ident not in new or ident in config_ids:
            merged.append(n)
        else:
            replacement = dict(new[ident])
            # Preserve explicitly disabled tabs and display positioning of existing nodes.
            if n.get('type') in ('tab', 'ui_tab') and 'disabled' in n:
                replacement['disabled'] = n['disabled']
            for key in ('x', 'y'):
                if key in n and key in replacement:
                    replacement[key] = n[key]
            merged.append(replacement)
    merged.extend(n for n in desired if n['id'] not in old)
    return merged


def ddl_statements(schema, fresh=False):
    matches = re.findall(r'CREATE TABLE IF NOT EXISTS `([A-Z0-9_]+)` \([\s\S]*?\) ENGINE=InnoDB;', schema)
    statements = re.findall(r'CREATE TABLE IF NOT EXISTS `[A-Z0-9_]+` \([\s\S]*?\) ENGINE=InnoDB;', schema)
    selected = dict(zip(matches, statements))
    if not all(t in selected for t in TABLES) or len(matches) != len(set(matches)):
        raise SetupError('Unexpected SQL schema; stopping')
    return selected if fresh else {name: selected[name] for name in TABLES}


def executable(*names):
    for name in names:
        if shutil.which(name):
            return shutil.which(name)
    raise SetupError('Required command not installed: ' + '/'.join(names))


def db_args(args, dump=False):
    command = [executable('mariadb-dump', 'mysqldump') if dump else executable('mariadb', 'mysql')]
    if args.mysql_options:
        p = Path(args.mysql_options).expanduser().resolve(strict=True)
        if not p.is_file() or stat.S_IMODE(p.stat().st_mode) & 0o077:
            raise SetupError('MySQL option file must be private (chmod 600)')
        command.append('--defaults-extra-file=' + str(p))
    command.extend(['--host', args.mysql_host, '--port', str(args.mysql_port)])
    return command


def check_database(args, statements):
    if not re.fullmatch(r'[A-Za-z0-9_]+', args.database):
        raise SetupError('Database name must contain only letters, digits and underscores')
    command = db_args(args) + ['--batch', '--skip-column-names', '--database', args.database]
    # Check all existing migration targets contain the expected columns before applying.
    existing = set(run(command, data=b'SHOW TABLES;').splitlines())
    for name, ddl in statements.items():
        if name in existing:
            expected = dict(re.findall(r'^\s*`([^`]+)`\s+((?:BIGINT|DOUBLE|DATETIME|INT|VARCHAR)(?:\(\d+\))?(?: UNSIGNED)?)', ddl, re.M))
            cols = run(command, data=f'SHOW COLUMNS FROM `{name}`;'.encode()).splitlines()
            def norm(value):
                return re.sub(r'\b(bigint|int)\(\d+\)', r'\1', value.lower())
            actual = {line.split('\t')[0]: norm(line.split('\t')[1]) for line in cols if '\t' in line}
            if any(actual.get(k) != norm(v) for k, v in expected.items()):
                raise SetupError(f'Existing {name} has incompatible columns; no destructive migration')
    return command, existing


def discover_flow(args):
    service = args.service
    if not re.fullmatch(r'[A-Za-z0-9_.@-]+\.service', service) or service.startswith('-'):
        raise SetupError('Invalid systemd service name')
    user = run(['systemctl', 'show', service, '--property=User', '--value']).strip()
    if not user:
        raise SetupError('Cannot identify Node-RED service user; custom/Docker installations need manual setup')
    home = Path(pwd.getpwnam(user).pw_dir)
    if args.flow_file:
        path = Path(args.flow_file).expanduser().resolve(strict=True)
    else:
        candidates = [p for p in (home / '.node-red').glob('flows*.json') if not p.name.endswith('_cred.json')]
        if len(candidates) != 1:
            raise SetupError('Ambiguous or missing flow file; supply --flow-file /absolute/path/flows.json')
        path = candidates[0].resolve(strict=True)
    if not path.is_file() or path.suffix != '.json' or path.stat().st_uid != pwd.getpwnam(user).pw_uid:
        raise SetupError('Flow file ownership/format does not match Node-RED service user')
    # Confirm the runtime's own path when a recent startup record is available.
    try:
        logs = run(['journalctl', '-u', service, '--no-pager', '-n', '2000'])
    except SetupError:
        logs = ''
    logged = re.findall(r'Flows file\s*:\s*(/[^\r\n]+\.json)\s*$', logs, re.M)
    if logged and Path(logged[-1]).resolve() != path:
        raise SetupError('Selected flow path differs from latest Node-RED startup log')
    if not logged and args.action != 'plan' and not args.flow_file:
        raise SetupError('Cannot confirm active flow file automatically; pass --flow-file after checking Node-RED startup log')
    return path, user


def digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_replace(path, data, metadata):
    fd, tmp = tempfile.mkstemp(prefix='.esp32-update-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            os.fchmod(stream.fileno(), stat.S_IMODE(metadata.st_mode))
            os.fchown(stream.fileno(), metadata.st_uid, metadata.st_gid)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def backup_file(source, target):
    with target.open('xb') as out:
        os.fchmod(out.fileno(), 0o600)
        with source.open('rb') as inp:
            shutil.copyfileobj(inp, out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'update', 'install'))
    parser.add_argument('--flow-file', help='Exact active Node-RED flow path (required if ambiguous)')
    parser.add_argument('--service', default='nodered.service')
    parser.add_argument('--database', default='nodered')
    parser.add_argument('--mysql-options', help='Private MySQL [client] option file, or use local socket auth')
    parser.add_argument('--install-palettes', action='store_true', help='Install missing palette packages during apply (requires npm/network)')
    parser.add_argument('--backup-dir', default='/var/backups/esp32radandsensors')
    parser.add_argument('--yes', action='store_true', help='Authorize service downtime and database migration')
    args = parser.parse_args()
    if args.action != 'plan' and (not args.yes or os.geteuid() != 0):
        raise SetupError('Use sudo and --yes only after reviewing plan')
    flow_path, user = discover_flow(args)
    desired = read_flow(ROOT / 'node-red/flows.json')
    original = flow_path.read_bytes()
    current = read_flow(flow_path)
    anchor = next(n['id'] for n in desired if n.get('type') == 'tab' and n.get('label') == 'ESP32 radiation and sensors')
    fresh = args.action == 'install' or (args.action == 'plan' and not any(n['id'] == anchor for n in current))
    merged = merge_flow(current, desired, fresh=fresh)
    statements = ddl_statements((ROOT / 'database/schema.sql').read_text(encoding='utf-8'), fresh=fresh)
    args.mysql_host, args.mysql_port = 'localhost', 3306
    for node in merged:
        if node.get('type') == 'Stackhero-MySQL-Server' and node['id'] in {n['id'] for n in desired}:
            if not fresh and node.get('database') != args.database:
                raise SetupError('Selected database differs from existing Node-RED config; specify --database')
            if fresh:
                node['database'] = args.database
            args.mysql_host = str(node.get('host', 'localhost'))
            try:
                args.mysql_port = int(node.get('port', 3306))
            except (TypeError, ValueError):
                raise SetupError('Invalid MySQL port in existing Node-RED config')
    if not args.mysql_host or args.mysql_host.startswith('-') or not 1 <= args.mysql_port <= 65535:
        raise SetupError('Invalid MySQL endpoint in existing Node-RED config')
    if args.mysql_host not in ('localhost', '127.0.0.1', '::1') and not args.mysql_options:
        raise SetupError('Remote database requires a private --mysql-options file for backup and migration')
    command, existing = check_database(args, statements)
    missing = [p for p in PALETTES if not (flow_path.parent / 'node_modules' / p / 'package.json').is_file()]
    print(f'Flow file: {flow_path} (user {user})')
    print(f'Mode: {"new project" if fresh else "update"}; nodes {len(current)} -> {len(merged)}; database {args.database}')
    print('Tables to create: ' + (', '.join(t for t in statements if t not in existing) or 'none'))
    if missing:
        print('Palette packages not detected locally: ' + ', '.join(missing))
        print('Install/verify them in Node-RED Palette Manager before applying.')
    if args.action == 'plan':
        print('PLAN ONLY — no mutations or service restarts')
        return
    if missing and not args.install_palettes:
        raise SetupError('Palette verification failed; install required libraries before applying')
    if missing:
        executable('npm'); executable('runuser')
    if run(['systemctl', 'is-active', args.service]).strip() != 'active':
        raise SetupError('Node-RED must be active before upgrade')
    if not missing and merged == current and all(t in existing for t in statements):
        print('Already up to date; no changes or restart'); return
    # Backups contain private database and credentials; never upload them.
    base = Path(args.backup_dir).expanduser().resolve()
    base.mkdir(mode=0o700, parents=True, exist_ok=True)
    if stat.S_IMODE(base.stat().st_mode) & 0o077:
        raise SetupError('Backup directory must be private (chmod 700)')
    backup = base / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup.mkdir(mode=0o700)
    metadata = flow_path.stat()
    stopped = changed = False
    try:
        run(['systemctl', 'stop', args.service]); stopped = True
        if flow_path.read_bytes() != original:
            raise SetupError('Flow changed after planning; restart and rerun')
        for source in [flow_path, flow_path.with_name(flow_path.stem + '_cred.json'),
                       flow_path.parent / 'settings.js', flow_path.parent / '.config.runtime.json',
                       flow_path.parent / 'package.json', flow_path.parent / 'package-lock.json']:
            if source.is_file():
                backup_file(source, backup / source.name)
        with (backup / 'database.sql').open('xb') as out:
            os.fchmod(out.fileno(), 0o600)
            run(db_args(args, dump=True) + ['--single-transaction', '--databases', args.database], output=out)
        if not (backup / 'database.sql').stat().st_size:
            raise SetupError('Empty database backup; aborting')
        print(f'Backup: {backup}')
        if missing:
            # Explicit opt-in; npm executes as the service user, never root.
            run([executable('runuser'), '-u', user, '--', executable('npm'), 'install', '--prefix', str(flow_path.parent), '--no-fund', '--no-audit', *missing])
            if any(not (flow_path.parent / 'node_modules' / p / 'package.json').is_file() for p in missing):
                raise SetupError('Palette installation verification failed')
        run(command, data=('\n'.join(statements.values())).encode())
        _, after = check_database(args, statements)
        if not set(statements).issubset(after):
            raise SetupError('Required tables missing after migration')
        if merged != current:
            updated = (json.dumps(merged, indent=2, ensure_ascii=False) + '\n').encode()
            atomic_replace(flow_path, updated, metadata); changed = True
            if digest(flow_path.read_bytes()) != digest(updated):
                raise SetupError('Flow integrity verification failed')
        run(['systemctl', 'start', args.service]); stopped = False
        if run(['systemctl', 'is-active', args.service]).strip() != 'active':
            raise SetupError('Node-RED did not return to active state')
    except BaseException:
        if changed:
            run(['systemctl', 'stop', args.service]); stopped = True
            atomic_replace(flow_path, original, metadata)
        if stopped:
            run(['systemctl', 'start', args.service])
        print(f'Failure: flow restored where possible; backup {backup}. Additive tables are never dropped.', file=sys.stderr)
        raise
    print('Backend updated; verify MQTT, dashboard and INSERTs before declaring end-to-end success.')
    if fresh:
        print('New project: set MQTT and MySQL credentials in the Node-RED editor, then Deploy.')


if __name__ == '__main__':
    try:
        main()
    except (SetupError, OSError, ValueError, KeyError) as error:
        print(f'Setup stopped: {error}', file=sys.stderr)
        sys.exit(1)
