#!/usr/bin/env python3
"""Independent finite planner/serial-admission plant; creates only its own PTY.

ACK means parser admission, not execution. Four planner slots and bounded100B
pending parser input expose write-age timeouts and final spindle-sync drains.
"""
from m3_commissioning_peer import Plant
import heapq
import math
import re
import time


class BackpressurePlant(Plant):
    def __init__(self):
        super().__init__()
        self.actual = [0., 0.]
        self.bias = self.offset = [0., 0.]
        self.home_actual = [0., 0.]
        self.physical_limits = [(0., 200.), (0., 150.)]
        self.planner = []
        self.parser = []
        self.depth = 4
        self.feed = 1500.
        self.drop_motion_ack = self.freeze_planner = self.cached_idle_speed = self.stuck_after_m5 = False
        self.final_m5_seen = self.motion_seen = False
        self.sim_time = 0.
        self.last_tick = time.monotonic()
        self.queue_tick()

    def queue_tick(self):
        self.sequence += 1
        heapq.heappush(self.scheduled, (self.ms()+20, self.sequence, {'action': '_plannerTick'}))

    def tick(self):
        now = time.monotonic()
        if self.state != 'Hold' and not self.freeze_planner:
            self.sim_time += now-self.last_tick
        self.last_tick = now
        while self.planner:
            block = self.planner[0]
            if block['startTime'] is None:
                block['startTime'] = self.sim_time
                block['from'] = list(self.actual)
            fraction = min(1., max(0., (self.sim_time-block['startTime'])/block['seconds']))
            self.actual = [a+(b-a)*fraction for a, b in zip(block['from'], block['to'])]
            if fraction < 1.:
                break
            self.planner.pop(0)
            self.event('motionCompleted', list(self.actual))
            if self.planner:
                self.planner[0]['startTime'] = block['startTime']+block['seconds']
                self.planner[0]['from'] = list(self.actual)
        self.admit()
        if self.state != 'Hold':
            self.state = 'Run' if self.planner else 'Idle'

    def admit(self):
        while self.parser and len(self.planner) < self.depth:
            line, received = self.parser.pop(0)
            match = re.fullmatch(r'G1 X([+-]?[0-9.]+) Y([+-]?[0-9.]+)(?: S([0-9.]+))?(?: F([0-9.]+))?', line)
            if not match:
                raise ValueError('fixture supports only the known XY motion words')
            target = [float(match[1])+self.offset[0], float(match[2])+self.offset[1]]
            if not all(low <= v <= high for v, (low, high) in zip(target, self.physical_limits)):
                self.event('collision', target)
                self.reply('ALARM:1\r\n')
                return
            if match[3] is not None:
                self.power = float(match[3])
            if match[4] is not None:
                self.feed = float(match[4])
            previous = self.planner[-1]['to'] if self.planner else self.actual
            seconds = max(.001, math.dist(previous, target)/self.feed*60.)
            self.motion_seen = True
            self.planner.append({'to': target, 'seconds': seconds, 'startTime': None, 'from': None})
            self.event('motionAdmitted', {'writeAgeMs': (time.monotonic_ns()-received)/1e6})
            if not self.drop_motion_ack:
                self.reply('ok\r\n')
                self.event('ack')
            else:
                self.event('droppedMotionAck')

    def status(self):
        if not self.cached_idle_speed:
            return super().status()
        if self.status_enabled:
            self.position = [a+b for a,b in zip(self.actual,self.bias)]
            x,y=self.position;ox,oy=self.offset
            cached=self.power if self.spindle==4 and self.motion_seen else (500. if self.stuck_after_m5 and self.final_m5_seen else 0.)
            self.reply(f'<{self.state}|MPos:{x},{y},0|WCO:{ox},{oy},0|FS:0,{cached}>\r\n')
            self.event('statusReply',self.state)
            if self.state=='Idle' and cached>0:self.event('idleCachedSpeed')

    def command(self, line):
        if line.startswith('G1 X'):
            self.parser.append((line, time.monotonic_ns()))
            if sum(len(value)+1 for value, _ in self.parser) > 100:
                raise ValueError('host exceeded bounded serial RX')
            self.admit()
            if self.state != 'Hold':
                self.state = 'Run'
            return
        if line == 'M5 S0' and self.spindle == 4:
            self.final_m5_seen = True
            self.event('finalM5', {'plannerBlocks': len(self.planner), 'parserBlocks': len(self.parser)})
            if self.planner or self.parser:
                # A real spindle_sync would block instead of ACKing here.
                self.event('prematureM5')
                return
        super().command(line)

    def receive(self, data):
        if 0x18 in data:
            self.planner.clear()
            self.parser.clear()
            self.final_m5_seen = self.motion_seen = False
        super().receive(data)

    def operate(self, request):
        if request.get('action') == '_plannerTick':
            self.tick()
            self.queue_tick()
            return {'accepted': True}
        if request.get('action') == 'plannerConfig':
            self.drop_motion_ack = bool(request.get('dropMotionAck', False))
            self.freeze_planner = bool(request.get('freeze', False))
            self.cached_idle_speed = bool(request.get('cachedIdleSpeed', False))
            self.stuck_after_m5 = bool(request.get('stuckAfterM5', False))
            return {'accepted': True}
        result = super().operate(request)
        if request.get('action') == 'snapshot':
            result.update(plannerBlocks=len(self.planner), parserBlocks=len(self.parser))
        return result


if __name__ == '__main__':
    peer = BackpressurePlant()
    try:
        peer.run()
    finally:
        peer.close()
