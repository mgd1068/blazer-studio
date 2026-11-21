#!/usr/bin/env python3
"""Independent timing peer for runtime tests; creates its own synthetic PTY only.

JSON-lines stdin controls, JSON-lines stdout replies. No caller-supplied endpoint
is accepted. This is deliberately not a physical GRBL/airflow model. It keeps
responding when the test's GUI thread is blocked and records monotonic wire
observations independently of Qt. Tests must supply separate synthetic machine
contracts; this process cannot commission anything.
"""
import collections
import heapq
import json
import math
import os
import re
import selectors
import sys
import termios
import time
import tty


class Peer:
    LIMIT = 65536

    def __init__(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave, termios.TCSANOW)
        os.set_blocking(self.master, False)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.master, selectors.EVENT_READ, 'wire')
        self.selector.register(sys.stdin.buffer, selectors.EVENT_READ, 'control')
        self.started = time.monotonic_ns()
        self.state = 'Idle'
        self.position = [0.0, 0.0]
        self.offset = [0.0, 0.0]
        self.status_enabled = self.ack_enabled = self.reset_enabled = True
        self.run_on_powered_command = False
        self.banner = False
        self.input = bytearray()
        self.controls = bytearray()
        self.pending = bytearray()
        self.events = collections.deque(maxlen=256)
        self.counts = collections.Counter()
        self.scheduled = []
        self.sequence = 0
        self.running = True

    def ms(self):
        return (time.monotonic_ns() - self.started) / 1_000_000

    def event(self, kind, value=None):
        self.counts[kind] += 1
        self.events.append({'ms': self.ms(), 'atNs': time.monotonic_ns(),
                            'kind': kind, 'value': value})

    @staticmethod
    def output(value):
        print(json.dumps(value, allow_nan=False), flush=True)

    def reply(self, data):
        if self.master < 0:
            return
        encoded = data.encode('ascii')
        if len(self.pending) + len(encoded) > self.LIMIT:
            raise ValueError('synthetic wire output exceeded budget')
        self.pending.extend(encoded)
        self.selector.modify(self.master, selectors.EVENT_READ | selectors.EVENT_WRITE, 'wire')

    def status(self):
        if self.status_enabled:
            x, y = self.position
            ox, oy = self.offset
            self.reply(f'<{self.state}|MPos:{x},{y},0|WCO:{ox},{oy},0|FS:0,0>\r\n')
            self.event('statusReply', self.state)

    def receive(self, data):
        for byte in data:
            if byte == ord('?'):
                self.event('query')
                self.status()
            elif byte in (ord('!'), ord('~'), 0x18):
                self.event('realtime', byte)
                if byte == ord('!'):
                    self.state = 'Hold'
                elif byte == ord('~'):
                    self.state = 'Run'
                else:
                    self.input.clear()
                    self.state = 'Idle'
                    self.banner = True
                    if self.reset_enabled:
                        self.reply("Grbl 1.3a ['$' for help]\r\n")
            elif byte >= 0x80:
                self.event('overrideByte', byte)  # No asserted firmware semantics.
            elif byte in (10, 13):
                if not self.banner:
                    self.banner = True
                    self.input.clear()
                    self.reply("Grbl 1.3a ['$' for help]\r\n")
                elif self.input:
                    line = self.input.decode('ascii')
                    self.input.clear()
                    self.event('command', line)
                    # Explicit timing fixture only, not GRBL planner/physical
                    # simulation. Remain busy even after M5 until reset/set.
                    if self.run_on_powered_command and re.match(r'^M[34](?: |$)|^G1(?: |$)', line):
                        power = re.search(r'(?:^| )S([+-]?[0-9]+(?:\.[0-9]+)?)(?: |$)', line)
                        if power and float(power.group(1)) > 0:
                            self.state = 'Run'
                            self.event('syntheticRunOnOutputRequest')
                    if line == '$I':
                        self.reply('[VER:1.3a.20211103:]\r\n')
                    elif line == '$G':
                        self.reply('[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\n')
                    elif line == '$$':
                        self.reply('$30=1000\r\n$31=0\r\n$32=1\r\n$22=1\r\n'
                                   '$13=0\r\n$130=200\r\n$131=150\r\n')
                    elif line == 'G10 L20 P1 X0 Y0':
                        self.offset = list(self.position)
                    if self.ack_enabled:
                        self.reply('ok\r\n')
                        self.event('ack')
            else:
                self.input.append(byte)
                if len(self.input) > 8192:
                    raise ValueError('synthetic command exceeded budget')

    def operate(self, request):
        action = request.get('action')
        if action == 'set':
            values = request['values']
            allowed = {'state', 'position', 'offset', 'statusEnabled', 'ackEnabled', 'resetEnabled',
                       'runOnPoweredCommand'}
            if not values or set(values) - allowed:
                raise ValueError('unknown synthetic setting')
            for key, value in values.items():
                if key == 'state':
                    if value not in ('Idle', 'Run', 'Hold', 'Alarm', 'Home'):
                        raise ValueError('invalid state')
                    self.state = value
                elif key in ('position', 'offset'):
                    if (not isinstance(value, list) or len(value) != 2 or
                        any(type(v) not in (int, float) or not math.isfinite(v) for v in value)):
                        raise ValueError('invalid XY')
                    setattr(self, key, value)
                else:
                    if type(value) is not bool:
                        raise ValueError('invalid switch')
                    setattr(self, {'statusEnabled': 'status_enabled', 'ackEnabled': 'ack_enabled',
                                  'resetEnabled': 'reset_enabled',
                                  'runOnPoweredCommand': 'run_on_powered_command'}[key], value)
            self.event('configured', values)
        elif action == 'schedule':
            delay = request['delayMs']
            operation = request['operation']
            if (type(delay) not in (int, float) or not math.isfinite(delay) or
                not 0 <= delay <= 10000 or len(self.scheduled) >= 128 or
                operation.get('action') not in ('set', 'reply', 'report', 'disconnect')):
                raise ValueError('invalid scheduled operation')
            self.sequence += 1
            heapq.heappush(self.scheduled, (self.ms() + delay, self.sequence, operation))
        elif action == 'reply':
            self.reply(request['data'])
            self.event('injectedReply')
        elif action == 'report':
            self.status()
        elif action == 'snapshot':
            return {'counts': dict(self.counts), 'events': list(self.events),
                    'state': self.state, 'ms': self.ms()}
        elif action == 'disconnect':
            if self.master >= 0:
                self.selector.unregister(self.master)
                os.close(self.master)
                self.master = -1
                self.pending.clear()
                self.event('disconnected')
        elif action == 'quit':
            self.running = False
        else:
            raise ValueError('unknown control action')
        return {'accepted': True}

    def run(self):
        self.output({'ready': True, 'port': os.ttyname(self.slave), 'pid': os.getpid(),
                     'syntheticOnly': True})
        while self.running:
            while self.scheduled and self.scheduled[0][0] <= self.ms():
                _, _, operation = heapq.heappop(self.scheduled)
                self.operate(operation)
            delay = max(0, (self.scheduled[0][0] - self.ms()) / 1000) if self.scheduled else 1
            for key, mask in self.selector.select(min(delay, 1)):
                if key.data == 'control':
                    data = os.read(key.fd, 4096)
                    if not data:
                        return
                    self.controls.extend(data)
                    if len(self.controls) > self.LIMIT:
                        raise ValueError('control input exceeded budget')
                    while b'\n' in self.controls:
                        line, _, tail = self.controls.partition(b'\n')
                        self.controls = bytearray(tail)
                        request = json.loads(line)
                        self.output({'id': request.get('id'), **self.operate(request)})
                else:
                    if mask & selectors.EVENT_READ:
                        data = os.read(self.master, 4096)
                        if data:
                            self.receive(data)
                    if mask & selectors.EVENT_WRITE and self.pending:
                        written = os.write(self.master, self.pending)
                        del self.pending[:written]
                        if not self.pending:
                            self.selector.modify(self.master, selectors.EVENT_READ, 'wire')

    def close(self):
        self.selector.close()
        if self.master >= 0:
            os.close(self.master)
        os.close(self.slave)


if __name__ == '__main__':
    peer = Peer()
    try:
        peer.run()
    finally:
        peer.close()
