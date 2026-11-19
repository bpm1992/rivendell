#!/usr/bin/env python3
#
# air_status_client.py
#
# A ripcd(8) status subscriber for testing (fork addition; see
# docs/apis/air_status.md).  Read-only unless told to send RML.
#
#   air_status_client.py watch [SECONDS]      print every ST message
#   air_status_client.py state                SQ, then summarize what comes
#   air_status_client.py send 'RML!' [WAIT]   send RML (UDP 5859) and print
#                                             the 'cmd' result for its #id
#   air_status_client.py selftest MACHINE     refusals and command IDs on
#                                             one machine; changes nothing
#                                             that plays
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License version 2 as
#   published by the Free Software Foundation.
#

import configparser
import os
import random
import socket
import string
import sys
import time

# Overridable for a test ripcd running beside the installed one
RIPCD_PORT = int(os.environ.get('RIPCD_PORT', 5006))
RML_PORT = int(os.environ.get('RML_PORT', 5859))


def password():
    conf = configparser.ConfigParser(strict=False, interpolation=None)
    conf.read('/etc/rd.conf')
    return conf.get('Identity', 'Password', fallback='')


class Ripcd:
    def __init__(self, host='127.0.0.1'):
        self.sock = socket.create_connection((host, RIPCD_PORT), timeout=5)
        self.buf = b''
        self.send('PW %s!' % password())
        reply = self.next(5)
        if reply != 'PW +':
            raise SystemExit('ripcd refused the password: %r' % reply)

    def send(self, cmd):
        self.sock.sendall(cmd.encode())

    def next(self, timeout):
        """The next message from ripcd, without its '!', or None."""
        end = time.monotonic() + timeout
        while b'!' not in self.buf:
            left = end - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                return None
            if not data:
                raise SystemExit('ripcd closed the connection')
            self.buf += data
        msg, self.buf = self.buf.split(b'!', 1)
        return msg.decode(errors='replace').strip()

    def status(self, timeout):
        """The next ST message as a list of fields, or None."""
        end = time.monotonic() + timeout
        while True:
            msg = self.next(max(0, end - time.monotonic()))
            if msg is None:
                return None
            if msg.startswith('ST '):
                return msg.split()


def send_rml(rml):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(rml.encode(), ('127.0.0.1', RML_PORT))
    s.close()


def new_id():
    return 't' + ''.join(random.choice(string.ascii_lowercase + string.digits)
                         for _ in range(8))


def summarize_state(f):
    i = 5
    while i < len(f):
        if f[i] == 'M':
            print('  machine %s: version %s mode %s running %s next %s'
                  % tuple(f[i + 1:i + 6]))
            i += 6
            lines = 0
        elif f[i] == 'D':
            print('    deck %s' % f[i + 1])
            i += 2
        elif f[i] == 'L':
            lines += 1
            if lines <= 3:
                print('    line %s' % f[i + 1])
            i += 2
        else:
            print('  ? %s' % f[i])
            i += 1


def cmd_result(r, cid, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        f = r.status(end - time.monotonic())
        if f is None:
            break
        if len(f) >= 8 and f[4] == 'cmd' and f[5] == cid:
            return f[6], f[7]
    return None


def selftest(mach):
    r = Ripcd()
    r.send('SS 1!')
    got = {}
    end = time.monotonic() + 3
    while time.monotonic() < end:
        f = r.status(end - time.monotonic())
        if f is None:
            break
        got.setdefault(f[4], f)
    failures = 0

    def check(cond, what):
        nonlocal failures
        print(('ok   ' if cond else 'FAIL ') + what)
        failures += 0 if cond else 1

    check('hello' in got, 'SS 1 gives a hello at once')
    check('state' in got, 'SS 1 gives a state at once')
    check('beat' in got, 'SS 1 gives a beat at once')
    if 'hello' in got:
        check('cmdid' in got['hello'][-1], 'caps list cmdid: %s'
              % got['hello'][-1])
    # Refusals, none of which changes the log or what plays
    cases = [
        ('LN %d 999999999' % mach, '0', 'noline'),
        ('LD %d 999999999' % mach, '0', 'noline'),
        ('LV %d 999999999 -1' % mach, '0', 'noline'),
        ('LP %d 999999999' % mach, '0', 'noline'),
        ('LI %d 999999999 1' % mach, '0', None),
        ('LN %d' % mach, '0', 'args'),
        ('LI %d -1 65003 sideways' % mach, '0', 'args'),
        ('PR %d' % mach, '0', 'idle'),
    ]
    for rml, ok, err in cases:
        cid = new_id()
        send_rml('%s #%s!' % (rml, cid))
        res = cmd_result(r, cid, 3)
        want = '%s %s' % (ok, err or '*')
        check(res is not None and res[0] == ok and (err is None or
                                                    res[1] == err),
              '%-34s -> %s (want %s)' % (rml, res, want))
    # A command without an ID gets no cmd message
    send_rml('LN %d 999999999!' % mach)
    f = r.status(1.5)
    check(f is None or f[4] != 'cmd', 'no ID, no cmd message')
    # SQ: a fresh state from the source within 2 seconds
    r.send('SQ!')
    fresh = False
    end = time.monotonic() + 3
    seen_states = 0
    while time.monotonic() < end:
        f = r.status(end - time.monotonic())
        if f is None:
            break
        if f[4] == 'state':
            seen_states += 1
            if seen_states >= 2:   # the one held, then the fresh one
                fresh = True
                break
    check(fresh, 'SQ brings a fresh state')
    print('selftest: %s' % ('PASS' if failures == 0 else
                            '%d FAILURES' % failures))
    return 1 if failures else 0


def main():
    if len(sys.argv) < 2:
        print(__doc__ if __doc__ else 'see the header of this file')
        return 2
    what = sys.argv[1]
    if what == 'watch':
        secs = float(sys.argv[2]) if len(sys.argv) > 2 else 60
        r = Ripcd()
        r.send('SS 1!')
        end = time.monotonic() + secs
        while time.monotonic() < end:
            f = r.status(end - time.monotonic())
            if f is None:
                break
            line = ' '.join(f)
            print(line if len(line) < 300 else line[:300] + ' ...(%d bytes)'
                  % len(line))
        return 0
    if what == 'state':
        r = Ripcd()
        r.send('SQ!')
        end = time.monotonic() + 3
        while time.monotonic() < end:
            f = r.status(end - time.monotonic())
            if f is None:
                break
            if f[4] == 'state':
                print('state from seq %s, %d fields' % (f[2], len(f)))
                summarize_state(f)
            elif f[4] in ('hello', 'beat', 'source'):
                print(' '.join(f))
        return 0
    if what == 'send':
        rml = sys.argv[2].rstrip('!')
        wait = float(sys.argv[3]) if len(sys.argv) > 3 else 3
        r = Ripcd()
        r.send('SS 1!')
        cid = new_id()
        send_rml('%s #%s!' % (rml, cid))
        print('%s #%s -> %s' % (rml, cid, cmd_result(r, cid, wait)))
        return 0
    if what == 'selftest':
        return selftest(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    print('unknown: %s' % what)
    return 2


if __name__ == '__main__':
    sys.exit(main())
