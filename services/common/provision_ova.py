#!/usr/bin/env python3
"""Create the appliance's first local user from VMware deployment properties."""
import json
import os
from pathlib import Path
import pwd
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

STATE = Path('/var/lib/cloud/green2-ova-provisioned.json')
RPC = Path('/snap/green2-nan-ble/current/usr/bin/vmware-rpctool')
PREFIX = 'guestinfo.green2.'


def rpc(command):
    result = subprocess.run([str(RPC), command], capture_output=True, text=True)
    return result.stdout.rstrip('\r\n') if result.returncode == 0 else ''


def read_properties():
    properties = {}
    xml = rpc('info-get guestinfo.ovfEnv')
    if xml:
        root = ET.fromstring(xml)
        for element in root.iter():
            if element.tag.rsplit('}', 1)[-1] != 'Property':
                continue
            attrs = {key.rsplit('}', 1)[-1]: value
                     for key, value in element.attrib.items()}
            if 'key' in attrs:
                properties[attrs['key']] = attrs.get('value', '')
    for field in ('username', 'password'):
        value = rpc('info-get ' + PREFIX + field)
        if value:
            properties[PREFIX + field] = value
    return properties


def validate(username, password):
    if not re.fullmatch(r'[a-z_][a-z0-9_-]{0,31}', username) or username == 'root':
        raise ValueError('Invalid local username')
    if not 8 <= len(password) <= 256 or any(c in password for c in '\0\r\n'):
        raise ValueError('Password must contain 8 to 256 characters and no line breaks')


def save_state(username, complete):
    STATE.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temp = STATE.with_suffix('.tmp')
    descriptor = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, 'w') as stream:
        json.dump({'username': username, 'complete': complete}, stream)
        stream.flush()
        os.fsync(stream.fileno())
    temp.replace(STATE)


def run(args, *, input=None):
    result = subprocess.run(args, input=input, text=True, capture_output=True)
    if result.returncode:
        # chpasswd input and hypervisor properties must never enter logs.
        raise RuntimeError(Path(args[0]).name + ' failed, status=' + str(result.returncode))


def main():
    previous = json.loads(STATE.read_text()) if STATE.exists() else {}
    if previous.get('complete'):
        return 0
    properties = read_properties()
    username = properties.get(PREFIX + 'username', 'green2')
    password = properties.get(PREFIX + 'password', '')
    if not password:
        print('Waiting for appliance username/password deployment properties', flush=True)
        return 75
    validate(username, password)
    try:
        account = pwd.getpwnam(username)
    except KeyError:
        account = None
    if account and (account.pw_uid < 1000 or previous.get('username') != username):
        raise ValueError('Refusing to replace an existing account')
    save_state(username, False)
    if account is None:
        run(['/usr/sbin/useradd', '--extrausers', '--create-home', '--shell',
             '/bin/bash', '--groups', 'adm,sudo', username])
    # Core's PAM stack writes the extrausers shadow database.
    run(['/usr/sbin/chpasswd'], input=username + ':' + password + '\n')
    sudoers = Path('/etc/sudoers.d/90-green2-ova')
    sudoers.write_text(username + ' ALL=(ALL:ALL) ALL\n')
    sudoers.chmod(0o440)
    save_state(username, True)
    rpc('info-set ' + PREFIX + 'password ""')
    print('Appliance account provisioned; password omitted from logs', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        # Exceptions only contain operation names, never property values.
        detail = str(error) if isinstance(error, (ValueError, RuntimeError)) else type(error).__name__
        print('Appliance account provisioning failed: ' + detail, file=sys.stderr)
        sys.exit(1)
