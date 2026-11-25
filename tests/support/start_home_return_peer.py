#!/usr/bin/env python3
"""Owned synthetic PTY for StartAttended Home/job/parking sequencing.

Adds deterministic ACK barriers; no caller-provided endpoint is ever opened.
All coordinate dynamics, G54/G53 interpretation and collisions remain in the
independent commissioning plant, not in the application planner.
"""
from autohome_connection_peer import AutoHomePlant
import re


class StartHomeReturnPlant(AutoHomePlant):
    def __init__(self):
        super().__init__()
        self.hold_park_m5 = False
        self.pending_park_m5 = False
        self.hold_powered_ack = False
        self.pending_powered_ack = False
        self.held_powered_once = False

    def command(self, line):
        park_barrier = (self.hold_park_m5 and line.replace(' ', '').startswith('M5')
                        and self.counts['powered'] > 0 and not self.pending_park_m5)
        power_words = re.findall(r'S([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))', line)
        effective_power = float(power_words[-1]) if power_words else self.power
        powered_barrier = (self.hold_powered_ack and not self.held_powered_once
                           and re.search(r'G0?1(?![0-9.])', line) and effective_power > 0)
        if park_barrier or powered_barrier:
            before = self.ack_enabled
            self.ack_enabled = False
            try:
                super().command(line)
            finally:
                self.ack_enabled = before
            if park_barrier:
                self.pending_park_m5 = True
                self.hold_park_m5 = False
                self.event('parkM5AckHeld', line)
            else:
                self.pending_powered_ack = True
                self.held_powered_once = True
                self.event('poweredAckHeld', line)
            return
        super().command(line)

    def operate(self, request):
        if request.get('action') == 'fixtureCommand':
            line = request.get('line')
            if not isinstance(line, str) or len(line) > 128:
                raise ValueError('bounded fixture command required')
            try:
                self.command(line)
            except ValueError as error:
                return {'accepted': False, 'reason': str(error)}
            return {'accepted': True}
        if request.get('action') == 'startSequenceFixture':
            self.hold_park_m5 = bool(request.get('holdParkM5', False))
            self.hold_powered_ack = bool(request.get('holdPoweredAck', False))
            return {'accepted': True}
        if request.get('action') == 'releaseParkM5':
            if not self.pending_park_m5:
                return {'accepted': False}
            self.pending_park_m5 = False
            self.reply('ok\r\n')
            self.event('ack')
            self.event('parkM5AckReleased')
            return {'accepted': True}
        if request.get('action') == 'releasePoweredAck':
            if not self.pending_powered_ack:
                return {'accepted': False}
            self.pending_powered_ack = False
            self.reply('ok\r\n')
            self.event('ack')
            self.event('poweredAckReleased')
            return {'accepted': True}
        return super().operate(request)


if __name__ == '__main__':
    peer = StartHomeReturnPlant()
    try:
        peer.run()
    finally:
        peer.close()
