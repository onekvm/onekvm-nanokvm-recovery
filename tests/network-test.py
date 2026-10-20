#!/usr/bin/env python3
"""Validate the JSON contract and lease transitions without touching host networking."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

helper, library = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
lib = ctypes.CDLL(library)
lib.recovery_network_request.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
base = dict(source='system', interface='eth0', mode='dhcp', address='', gateway='',
            fallback_enabled=True, fallback_address='192.168.233.10/24',
            mac_address='02:12:34:56:78:90', use_custom_dns=False, dns=[],
            dhcp_client_id='01021234567890')
def encoded(network):
    payload = json.dumps(network, separators=(',', ':')).encode()
    return json.dumps(dict(version=1, network=network, sha256=hashlib.sha256(payload).hexdigest()), separators=(',', ':')).encode()
def check(path):
    return subprocess.run([helper, 'check', str(path)], capture_output=True).returncode == 0
with tempfile.TemporaryDirectory(prefix='onekvm-recovery-network-') as directory:
    work = Path(directory)
    state = work/'state.json'
    state.write_bytes(encoded(base))
    assert check(state), 'canonical Rust/Python field order and SHA-256 must be accepted by C'
    assert subprocess.check_output([helper, 'get', str(state), 'mac_address'], text=True).strip() == base['mac_address']
    manual = {**base, 'use_custom_dns':True, 'dns':['10.0.0.'+str(i) for i in range(1,7)]}
    state.write_bytes(encoded(manual)); assert check(state), 'preserve six system DNS servers'
    for field, value in [('interface','../../eth0'),('interface','lo'),('mode','shell'),('address','$(touch /tmp/pwned)'),('mac_address','01:12:34:56:78:90'),('fallback_address','127.0.0.1/24'),('fallback_address','192.168.1.1/33'),('fallback_enabled','true'),('dns',['tls://1.1.1.1']),('dhcp_client_id','invalid')]:
        state.write_bytes(encoded({**base,field:value})); assert not check(state),(field,value)
    state.write_bytes(encoded(base).replace(b'"version":1',b'"version":2')); assert not check(state)
    state.write_bytes(encoded(base).replace(b'233.10',b'233.11')); assert not check(state), 'checksum protects complete but damaged JSON'
    state.write_bytes(b'x'*4097); assert not check(state)
    request = json.dumps({**base,'mode':'static','address':'10.20.30.40/24','fallback_enabled':False}).encode()
    assert lib.recovery_network_request(request,len(request),os.fsencode(state)) == 0
    assert json.loads(state.read_text())['network']['source'] == 'session'
    assert check(state)
    assert lib.recovery_network_request(request[:-4],len(request)-4,os.fsencode(state)) < 0
    unknown = json.dumps({**base,'unexpected':'x'}).encode()
    assert lib.recovery_network_request(unknown,len(unknown),os.fsencode(state)) < 0

    # Run the production lease script with a recording ip command and isolated files.
    script = (Path(__file__).resolve().parents[1]/'initramfs/onekvm-recovery-dhcp').read_text()
    resolver = work/'resolv.conf'; lease = work/'lease'; log = work/'commands'
    script = script.replace('/usr/sbin/onekvm-recovery-network-config',helper).replace('/run/onekvm-recovery-network.json',str(state)).replace('/run/onekvm-recovery-lease',str(lease)).replace('/etc/resolv.conf',str(resolver))
    script = script.replace('/run/onekvm-recovery-network.lock',str(work/'lock')).replace('/run/onekvm-recovery-dhcp.generation',str(work/'generation'))
    test_script=work/'dhcp'; test_script.write_text(script)
    bin_dir=work/'bin';bin_dir.mkdir()
    (bin_dir/'ip').write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$NETWORK_TEST_LOG"\n')
    (bin_dir/'ip').chmod(0o755)
    env={**os.environ,'PATH':str(bin_dir)+':'+os.environ['PATH'],'NETWORK_TEST_LOG':str(log),'interface':'eth0','ip':'10.20.30.50','subnet':'255.255.255.0','router':'10.20.30.1','dns':'10.20.30.53'}
    state.write_bytes(encoded(base)); resolver.write_text(''); lease.write_text('10.20.30.49/24\n')
    for event in ['bound','renew','deconfig']:
        subprocess.run(['sh',str(test_script),event],env=env,check=True)
        calls=log.read_text(); assert 'addr flush' not in calls
        assert 'addr del 192.168.233.10' not in calls
        assert 'addr replace 192.168.233.10/24 dev eth0' in calls
    assert resolver.read_text() == ''
    state.write_bytes(encoded(manual)); resolver.write_text('nameserver 10.0.0.1\n')
    subprocess.run(['sh',str(test_script),'bound'],env=env,check=True)
    subprocess.run(['sh',str(test_script),'deconfig'],env=env,check=True)
    assert resolver.read_text() == 'nameserver 10.0.0.1\n', 'DHCP must preserve custom DNS'
    env['subnet']='255.0.255.0'
    before=log.read_text();assert subprocess.run(['sh',str(test_script),'renew'],env=env).returncode != 0
    assert log.read_text()==before, 'bad lease must not change the existing network'
    (work/'generation').write_text('new-generation')
    env['ONEKVM_RECOVERY_DHCP_ID']='old-generation'
    assert subprocess.run(['sh',str(test_script),'deconfig'],env=env).returncode == 0
    assert log.read_text()==before, 'late lease callbacks must not touch the next configuration'
print('Recovery snapshot validation, checksum, DNS and DHCP fallback transitions passed')
