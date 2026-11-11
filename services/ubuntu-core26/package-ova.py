#!/usr/bin/env python3
"""Add deployment properties to an OVF and assemble a checked OVA archive."""
import argparse
import hashlib
from pathlib import Path
import tarfile
import xml.etree.ElementTree as ET

OVF = 'http://schemas.dmtf.org/ovf/envelope/1'
ET.register_namespace('ovf', OVF)
ET.register_namespace('rasd', 'http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_ResourceAllocationSettingData')
ET.register_namespace('vssd', 'http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_VirtualSystemSettingData')
ET.register_namespace('vmw', 'http://www.vmware.com/schema/ovf')
ET.register_namespace('xsi', 'http://www.w3.org/2001/XMLSchema-instance')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def package(source, output):
    tree = ET.parse(source)
    system = tree.getroot().find('{' + OVF + '}VirtualSystem')
    if system is None:
        raise ValueError('Expected one VirtualSystem')
    if system.find('{' + OVF + '}ProductSection') is not None:
        raise ValueError('ProductSection already exists; use a clean OVF export')
    section = ET.SubElement(system, '{' + OVF + '}ProductSection')
    ET.SubElement(section, '{' + OVF + '}Info').text = 'First-boot account configuration'
    ET.SubElement(section, '{' + OVF + '}Product').text = 'Green2 Ubuntu Core 26'
    ET.SubElement(section, '{' + OVF + '}Version').text = '20261111.ova.1'
    for key, label, description, default, secret in [
        ('username', 'Login username', 'Local administrator username, created on first boot.', 'green2', False),
        ('password', 'Initial login password', 'Choose 8 to 256 characters. Empty leaves account provisioning pending.', '', True),
    ]:
        attrs = {'{' + OVF + '}key': 'guestinfo.green2.' + key,
                 '{' + OVF + '}type': 'string', '{' + OVF + '}value': default,
                 '{' + OVF + '}userConfigurable': 'true'}
        if secret:
            attrs['{' + OVF + '}password'] = 'true'
        prop = ET.SubElement(section, '{' + OVF + '}Property', attrs)
        ET.SubElement(prop, '{' + OVF + '}Label').text = label
        ET.SubElement(prop, '{' + OVF + '}Description').text = description
    for hardware in system.findall('{' + OVF + '}VirtualHardwareSection'):
        hardware.set('{' + OVF + '}transport', 'com.vmware.guestInfo')
    tree.write(source, encoding='utf-8', xml_declaration=True)
    files = []
    for item in tree.getroot().findall('./{' + OVF + '}References/{' + OVF + '}File'):
        name = item.get('{' + OVF + '}href')
        if not name or Path(name).name != name:
            raise ValueError('Referenced files must be package-local basenames')
        path = source.parent / name
        if not path.is_file():
            raise FileNotFoundError(path)
        files.append(path)
    manifest = source.with_suffix('.mf')
    manifest.write_text(''.join('SHA256(' + p.name + ')= ' + sha(p) + '\n'
                                for p in [source, *files]))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, 'w', format=tarfile.USTAR_FORMAT) as archive:
        for path in [source, manifest, *files]:
            archive.add(path, arcname=path.name, recursive=False)
    output.with_suffix(output.suffix + '.sha256').write_text(sha(output) + '  ' + output.name + '\n')
    print('OVA packaged with configurable username/password; password default is empty.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('ovf', type=Path)
    parser.add_argument('ova', type=Path)
    args = parser.parse_args()
    package(args.ovf.resolve(), args.ova.resolve())
