#!/usr/bin/env python3
"""Own-PTY plant for automatic-Home admission tests; never opens a real port.

The only added firmware identity is explicitly synthetic. Holding the standalone
M5 ACK preserves normal command execution while exposing the exact boundary
between an accepted Home request and the later physical $H write.
"""
from m3_commissioning_peer import Plant


class AutoHomePlant(Plant):
    def __init__(self):
        super().__init__()
        self.hold_home_m5 = False
        self.pending_home_m5 = False

    def command(self, line):
        if line == '$I':
            self.reply('[MSG:synthetic:M3:MAC=02:11:22:33:44:55]\r\n')
        if line == 'M5' and self.hold_home_m5:
            previous = self.ack_enabled
            self.ack_enabled = False
            try:
                super().command(line)
            finally:
                self.ack_enabled = previous
            self.pending_home_m5 = True
            self.event('homeM5AckHeld')
            return
        super().command(line)

    def operate(self, request):
        if request.get('action') == 'holdHomeM5':
            self.hold_home_m5 = bool(request.get('enabled', True))
            return {'accepted': True}
        if request.get('action') == 'releaseHomeM5':
            if not self.pending_home_m5:
                return {'accepted': False}
            self.pending_home_m5 = False
            self.hold_home_m5 = False
            self.reply('ok\r\n')
            self.event('ack')
            self.event('homeM5AckReleased')
            return {'accepted': True}
        return super().operate(request)


if __name__ == '__main__':
    peer = AutoHomePlant()
    try:
        peer.run()
    finally:
        peer.close()
