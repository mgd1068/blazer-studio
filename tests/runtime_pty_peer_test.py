#!/usr/bin/env python3
"""Exercise the independent PTY fixture without Qt or any actual serial port."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time
import unittest


class RuntimePeerTest(unittest.TestCase):
    def setUp(self):
        script = Path(__file__).parent / 'support/runtime_pty_peer.py'
        self.peer = subprocess.Popen([sys.executable, '-u', str(script)], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.buffer = bytearray()
        ready = self.read_json()
        self.assertTrue(ready['syntheticOnly'])
        self.assertNotEqual(ready['pid'], os.getpid())
        self.port = os.open(ready['port'], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.sequence = 0

    def tearDown(self):
        os.close(self.port)
        self.peer.stdin.close()
        try:
            status = self.peer.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.peer.kill()
            self.peer.wait()
            self.fail('independent peer did not exit after control EOF')
        error = self.peer.stderr.read().decode()
        self.peer.stdout.close()
        self.peer.stderr.close()
        self.assertEqual(status, 0, error)

    def read_json(self):
        deadline = time.monotonic() + 3
        while b'\n' not in self.buffer:
            self.assertTrue(select.select([self.peer.stdout], [], [],
                                         max(0, deadline-time.monotonic()))[0], 'peer JSON timeout')
            data = os.read(self.peer.stdout.fileno(), 65536)
            self.assertTrue(data, 'peer stdout closed')
            self.buffer.extend(data)
        line, _, tail = self.buffer.partition(b'\n')
        self.buffer = bytearray(tail)
        return json.loads(line)

    def control(self, action, **fields):
        self.sequence += 1
        self.peer.stdin.write((json.dumps(dict(action=action, id=self.sequence, **fields))+'\n').encode())
        self.peer.stdin.flush()
        result = self.read_json()
        self.assertEqual(result['id'], self.sequence)
        return result

    def wire(self, expected):
        data = bytearray()
        deadline = time.monotonic() + 3
        while expected not in data:
            self.assertTrue(select.select([self.port], [], [],
                                         max(0, deadline-time.monotonic()))[0], repr(data))
            data.extend(os.read(self.port, 65536))
        return bytes(data)

    def test_handshake_and_realtime_observed_by_independent_process(self):
        os.write(self.port, b'\r\n')
        self.wire(b'Grbl 1.3a')
        os.write(self.port, b'$I\n$$\n$G\n?')
        actual = self.wire(b'<Idle|MPos:0.0,0.0,0|WCO:0.0,0.0,0|FS:0,0>')
        self.assertIn(b'$130=200\r\n$131=150\r\n', actual)
        self.assertIn(b'[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]', actual)
        os.write(self.port, b'!\x18')
        self.wire(b'Grbl 1.3a')
        snapshot = self.control('snapshot')
        self.assertEqual(snapshot['counts']['ack'], 3)
        self.assertEqual([e['value'] for e in snapshot['events'] if e['kind']=='realtime'], [33, 24])

    def test_scheduled_reply_and_state_change_run_while_parent_is_blocked(self):
        self.control('schedule', delayMs=50, operation={'action':'set','values':{'state':'Run'}})
        self.control('schedule', delayMs=80, operation={'action':'report'})
        self.control('schedule', delayMs=150, operation={'action':'set','values':{'state':'Hold'}})
        self.control('schedule', delayMs=180, operation={'action':'report'})
        # Deliberately no parent Qt/event/pipe/serial work until after this sleep.
        time.sleep(0.5)
        parent_return = time.monotonic_ns()
        snapshot = self.control('snapshot')
        reports = [e for e in snapshot['events'] if e['kind']=='statusReply']
        self.assertEqual([e['value'] for e in reports], ['Run', 'Hold'])
        self.assertTrue(all(e['atNs'] < parent_return for e in reports))
        actual = self.wire(b'<Hold|')
        self.assertIn(b'<Run|', actual)

    def test_status_silence_does_not_suppress_ack_or_realtime_journal(self):
        os.write(self.port, b'\n')
        self.wire(b'Grbl 1.3a')
        self.control('set', values={'statusEnabled':False})
        os.write(self.port, b'?M5 S0\n!')
        self.assertEqual(self.wire(b'ok\r\n'), b'ok\r\n')
        snapshot = self.control('snapshot')
        self.assertEqual(snapshot['counts']['query'], 1)
        self.assertEqual(snapshot['counts'].get('statusReply', 0), 0)
        self.assertEqual(snapshot['counts']['ack'], 1)
        self.assertEqual(snapshot['counts']['realtime'], 1)

    def test_fragmented_wire_input_and_bounded_journal(self):
        os.write(self.port, b'\n')
        self.wire(b'Grbl 1.3a')
        os.write(self.port, b'G10 L20 P1 ')
        os.write(self.port, b'X0 Y0\n')
        self.wire(b'ok\r\n')
        # No status replies: its PTY output cannot fill while the parent waits.
        self.control('set', values={'statusEnabled':False})
        os.write(self.port, b'?'*600 + b'M5\n')
        self.wire(b'ok\r\n')
        snapshot = self.control('snapshot')
        self.assertEqual(snapshot['counts']['query'], 600)
        self.assertEqual(len(snapshot['events']), 256)
        self.assertEqual([e['value'] for e in snapshot['events'] if e['kind']=='command'], ['M5'])

    def test_peer_hangup_occurs_before_parent_returns(self):
        self.control('schedule', delayMs=50, operation={'action':'disconnect'})
        time.sleep(0.2)
        parent_return = time.monotonic_ns()
        snapshot = self.control('snapshot')
        self.assertEqual(snapshot['counts']['disconnected'], 1)
        event = next(e for e in snapshot['events'] if e['kind']=='disconnected')
        self.assertLess(event['atNs'], parent_return)
        self.assertEqual(os.read(self.port, 1), b'')

    def test_explicit_synthetic_run_mode_keeps_ack_and_completion_separate(self):
        os.write(self.port, b'\n')
        self.wire(b'Grbl 1.3a')
        os.write(self.port, b'M4 S10\n')
        self.wire(b'ok\r\n')
        self.assertEqual(self.control('snapshot')['state'], 'Idle') # Default unchanged.
        self.control('set', values={'runOnPoweredCommand':True})
        os.write(self.port, b'M4 S0\n')
        self.wire(b'ok\r\n')
        self.assertEqual(self.control('snapshot')['state'], 'Idle')
        os.write(self.port, b'M4 S10\nM5 S0\n')
        self.wire(b'ok\r\nok\r\n')
        snapshot = self.control('snapshot')
        self.assertEqual(snapshot['state'], 'Run')
        self.assertEqual(snapshot['counts']['syntheticRunOnOutputRequest'], 1)
        os.write(self.port, b'\x18')
        self.wire(b'Grbl 1.3a')
        self.assertEqual(self.control('snapshot')['state'], 'Idle')


if __name__ == '__main__':
    unittest.main()
